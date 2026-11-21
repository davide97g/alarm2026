#include "alarm.h"
#include "../../include/config.h"
#include "../audio/audio.h"
#include "../led/led.h"
#include "../input/input.h"
#include "../watchdog/watchdog.h"
#include "../logic/logic.h"
#include "../sensors/Sensors.h" // rtc, rtcLocalTime, rtc_setupAlarm2

extern char trackNames[NUM_TRACKS][64];
extern uint8_t actualNumTracks;

void handleAlarm(SystemState &appState) {
    Serial.println("Inizio ciclo riproduzione Audio/LED (BLOCCANTE)!");

    bool audioStarted = false;
    const char* track = nullptr;
    if (actualNumTracks > 0) {
        // FORZA SPEGNIMENTO BLUETOOTH per dare priorità assoluta alla sveglia
        if (appState.bluetoothEnabled) {
            Serial.println("[ALARM] Sgancio forzato del Bluetooth per priorità Sveglia!");
            appState.bluetoothEnabled = false;
            toggleBluetooth(false, "");
            ui_update(appState, false);
        }

        track = trackNames[appState.currentTrackIndex % actualNumTracks];
        Serial.printf("[ALARM] Riprodurrò traccia: %s (indice %d)\n", track, appState.currentTrackIndex);
        setVolume(appState.alarmVolume); // Imposta il volume da 1 a 10
        audioStarted = playMp3(track);
        if (!audioStarted) {
            Serial.printf("[ALARM] ERRORE: impossibile riprodurre %s, procedo solo LED\n", track);
            appState.audioError = true;
        }
    } else {
        Serial.println("[ALARM] ERRORE: Nessun file MP3 trovato in memoria. Procedo solo LED.");
        appState.audioError = true;
    }

    int64_t alarmStartTimeUs = esp_timer_get_time();
    int64_t autoStopUs = (int64_t)appState.autoStopTime * 60LL * 1000000LL;
    int64_t lastUiUpdateUs = 0; // Timer per aggiornamento orario

    appState.currentScreen = SCREEN_ALARM;

    // Timeout hard di 10 minuti come sicurezza estrema
    const int64_t hardTimeoutUs = 600000000LL;
    if (autoStopUs > hardTimeoutUs || autoStopUs <= 0) {
        autoStopUs = hardTimeoutUs;
    }

    bool isSnoozed = false;
    uint32_t stuckLoopCounter = 0;

    // Massima luminosità della rampa, con la stessa curva di led_on()
    uint8_t max_duty = appState.ledBrightness > 0 ? ledCurveDuty(appState.ledBrightness) : 0;

    // Parte da LED spento: evita il flash a piena potenza se era già acceso
    led_set_duty(0);

    while (true) {
        watchdog_feed();
        int64_t nowUs = esp_timer_get_time();

        // Rampa lineare 0 -> max -> 0 in 12 secondi
        led_set_duty(alarmFadeDuty(max_duty, nowUs - alarmStartTimeUs));

        // Protezione contro loop infinito non bloccante
        if (++stuckLoopCounter > 100000000) {
            Serial.println("[CRITICAL ERROR] Loop suoneria bloccato! Uscita di emergenza...");
            break;
        }

        // Ridisegna l'orologio solo al cambio minuto (partial refresh)
        if (nowUs - lastUiUpdateUs > 1000000LL) {
            lastUiUpdateUs = nowUs;
            struct tm local = rtcLocalTime();
            if (local.tm_min != appState.minute) {
                appState.hour = local.tm_hour;
                appState.minute = local.tm_min;
                ui_update(appState, false);
            }
        }

        InputEvent event = input_get_event(0);
        if (event == INPUT_BTN_A_SHORT || event == INPUT_BTN_A_LONG) { // Tasto A -> SNOOZE
            Serial.println("Azione: SNOOZE!");
            isSnoozed = true;
            break;
        }
        if (event == INPUT_BTN_B_SHORT || event == INPUT_BTN_B_LONG) { // Tasto B -> STOP
            Serial.println("Azione: STOP!");
            break;
        }
        if (nowUs - alarmStartTimeUs > autoStopUs) {
            Serial.println("Azione: AUTO-STOP (Tempo scaduto o Hard Timeout)!");
            break;
        }

        // Traccia finita: riavviala per un loop continuo
        if (audioStarted && !loopAudio()) {
            audioStarted = playMp3(track);
            if (!audioStarted) {
                Serial.println("[ALARM] Avviso: Errore nel riavvio del loop audio. Continuo solo con animazione LED.");
                appState.audioError = true;
            }
        }

        delay(2); // La decodifica MP3 richiede CPU costante per non balbettare
    }

    stopAudio();
    led_off(true);
    appState.currentScreen = SCREEN_HOME;

    // Snooze: si torna a main.cpp, che aggiorna la UI ed entra in Light Sleep
    appState.isSnoozeActive = isSnoozed;
    if (isSnoozed) {
        struct tm local = rtcLocalTime();
        snoozeTarget(local.tm_hour, local.tm_min, appState.snoozeTime,
                     &appState.snoozeTargetHour, &appState.snoozeTargetMinute);
        Serial.printf("Snooze attivato. Prossima suoneria alle %02d:%02d (Locale)\n",
                      appState.snoozeTargetHour, appState.snoozeTargetMinute);
    }
}

// Imposta ora/minuto locali su t e lascia a mktime() la scelta dell'ora legale
static time_t localTargetEpoch(struct tm& t, uint8_t hour, uint8_t minute) {
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    return mktime(&t);
}

// Il DS3231 lavora in UTC: converte e programma l'Allarme 2
static void armAlarm2(time_t targetEpoch, bool log) {
    struct tm* utc = gmtime(&targetEpoch);
    if (log) Serial.printf("[ALARM] Programmato RTC in UTC: %02d:%02d\n", utc->tm_hour, utc->tm_min);
    rtc_setupAlarm2(utc->tm_hour, utc->tm_min);
}

static void disableAlarm2() {
    rtc.disableAlarm(2);
    rtc.clearAlarm(2);
}

void calculateNextAlarm(SystemState &appState) {
    if (!appState.alarmsGloballyEnabled) {
        Serial.println("Sveglie globalmente disattivate. Nessun allarme inviato all'RTC.");
        appState.isSnoozeActive = false;
        disableAlarm2();
        appState.nextAlarmFound = false;
        return;
    }

    time_t utc_epoch;
    struct tm local = rtcLocalTime(&utc_epoch);

    // Snooze in corso: mantiene il suo orario
    if (appState.isSnoozeActive) {
        Serial.printf("[ALARM] Snooze in corso. Mantengo Allarme 2 alle %02d:%02d\n", appState.snoozeTargetHour, appState.snoozeTargetMinute);
        appState.nextAlarmFound = true;
        appState.nextAlarmHour = appState.snoozeTargetHour;
        appState.nextAlarmMinute = appState.snoozeTargetMinute;

        time_t target = localTargetEpoch(local, appState.snoozeTargetHour, appState.snoozeTargetMinute);
        if (target <= utc_epoch) { // Già passato oggi: domani
            local.tm_mday += 1;
            local.tm_isdst = -1;
            target = mktime(&local);
        }
        armAlarm2(target, false);
        return;
    }

    NextAlarm next = findNextAlarm(appState.alarms, appState.numConfiguredAlarms,
                                   local.tm_wday, local.tm_hour * 60 + local.tm_min);
    appState.nextAlarmFound = next.found;
    if (next.found) {
        Serial.printf("[ALARM] Prossima sveglia calcolata tra %u minuti (alle %02d:%02d Locale)\n", next.minutesAway, next.hour, next.minute);
        appState.nextAlarmHour = next.hour;
        appState.nextAlarmMinute = next.minute;
        local.tm_mday += next.dayOffset;
        armAlarm2(localTargetEpoch(local, next.hour, next.minute), true);
    } else {
        Serial.println("[ALARM] Nessuna sveglia attiva per i prossimi giorni. Disattivo Allarme 2.");
        disableAlarm2();
    }
}
