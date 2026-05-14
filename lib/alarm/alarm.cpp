#include "alarm.h"
#include "../../include/config.h"
#include "../audio/audio.h"
#include "../led/led.h"
#include "../input/input.h"
#include "../watchdog/watchdog.h"
#include "../power/power.h"
#include <RTClib.h>  // Per DateTime e rtc
#include <math.h>    // Per sin() e fmin() per l'animazione LED

#include "../sensors/Sensors.h" // Per ottenere rtc, rtc_setupAlarm2 in modo sicuro

extern char trackNames[NUM_TRACKS][64];
extern uint8_t actualNumTracks;
extern void toggleBluetooth(bool enable, const char* deviceName);
extern void ui_update(const SystemState& newState, bool forceFullRefresh);

// Importo le variabili di stato per lo Snooze da main.cpp
extern bool isSnoozeActive;
extern uint8_t snoozeTargetHour;
extern uint8_t snoozeTargetMinute;

void handleAlarm(SystemState &appState) {
    Serial.println("Inizio ciclo riproduzione Audio/LED (BLOCCANTE)!");
    
    bool audioStarted = false;
    if (actualNumTracks > 0) {
        // FORZA SPEGNIMENTO BLUETOOTH per dare priorità assoluta alla sveglia
        if (appState.bluetoothEnabled) {
            Serial.println("[ALARM] Sgancio forzato del Bluetooth per priorità Sveglia!");
            appState.bluetoothEnabled = false;
            toggleBluetooth(false, ""); 
            ui_update(appState, false);
        }

        const char* trackToPlay = trackNames[appState.currentTrackIndex % actualNumTracks];
        Serial.printf("[ALARM] Riprodurrò traccia: %s (indice %d)\n", trackToPlay, appState.currentTrackIndex);
        setVolume(appState.alarmVolume); // Imposta il volume da 1 a 10
        audioStarted = playMp3(trackToPlay);
        if (!audioStarted) {
            Serial.printf("[ALARM] ERRORE: impossibile riprodurre %s, procedo solo LED\n", trackToPlay);
            appState.audioError = true;
        }
    } else {
        Serial.println("[ALARM] ERRORE: Nessun file MP3 trovato in memoria. Procedo solo LED.");
        appState.audioError = true;
    }

    int64_t alarmStartTimeUs = esp_timer_get_time();
    int64_t autoStopUs = (int64_t)appState.autoStopTime * 60LL * 1000000LL;
    int64_t lastUiUpdateUs = 0; // Timer per animazione e aggiornamento orario
    
    // Imposta la schermata corrente su quella della sveglia
    appState.currentScreen = SCREEN_ALARM;

    // Timeout hard di 10 minuti (600.000.000 us) come sicurezza estrema
    int64_t hardTimeoutUs = 600000000LL;
    if (autoStopUs > hardTimeoutUs || autoStopUs <= 0) {
        autoStopUs = hardTimeoutUs;
    }

    bool isSnoozed = false;
    uint32_t stuckLoopCounter = 0;
    
    // Calcola la massima luminosità (duty cycle) una sola volta, basandosi sulla mappatura
    // non-lineare presente in led.cpp per coerenza visiva.
    uint8_t max_duty = 0;
    if (appState.ledBrightness > 0) {
        float t_bri = (appState.ledBrightness - 1.0f) / 9.0f;
        float curve_bri = pow(t_bri, 2.0f);
        max_duty = round(5.0f + (250.0f * curve_bri));
    }

    // FIX: Assicura che il LED parta da spento prima di iniziare il ciclo di animazione.
    // Questo previene che la luce "flashi" a piena potenza per un istante se era già accesa.
    led_set_duty(0);

    while (true) {
        watchdog_feed();
        
        int64_t nowUs = esp_timer_get_time();

        // --- LOGICA LED: Rampa lineare (0 -> max -> 0 in 12 secondi) ---
        const int64_t halfCycleTimeUs = 6000000LL; // 6 secondi per salita/discesa
        const int64_t cycleTimeUs = halfCycleTimeUs * 2; // 12 secondi totali
        
        int64_t elapsedUs = nowUs - alarmStartTimeUs;
        int64_t timeInCycleUs = elapsedUs % cycleTimeUs;
        
        float fadeIndex = 0.0f;
        if (timeInCycleUs < halfCycleTimeUs) {
            // Rampa di salita (0 -> 3 secondi)
            fadeIndex = (float)timeInCycleUs / (float)halfCycleTimeUs;
        } else {
            // Rampa di discesa (3 -> 6 secondi)
            int64_t timeInRampDown = timeInCycleUs - halfCycleTimeUs;
            fadeIndex = 1.0f - ((float)timeInRampDown / (float)halfCycleTimeUs);
        }
        
        // Applica l'indice di dissolvenza alla luminosità massima e imposta il LED
        led_set_duty(round((float)max_duty * fadeIndex));

        // Protezione contro loop infinito non bloccante
        stuckLoopCounter++;
        if (stuckLoopCounter > 100000000) {
            Serial.println("[CRITICAL ERROR] Loop suoneria bloccato! Uscita di emergenza...");
            break;
        }

        // --- Aggiornamento UI (Orologio) ---
        // Controlliamo periodicamente se è scattato il minuto per aggiornare il display
        if (nowUs - lastUiUpdateUs > 1000000LL) {
            lastUiUpdateUs = nowUs;
            
            DateTime utc_now = rtc.now();
            time_t utc_epoch = utc_now.unixtime();
            struct tm* local_timeinfo = localtime(&utc_epoch);
            
            if (local_timeinfo->tm_min != appState.minute) {
                appState.hour = local_timeinfo->tm_hour;
                appState.minute = local_timeinfo->tm_min;
                ui_update(appState, false); // Lancia il partial refresh solo al cambio minuto
            }
        }

        InputEvent event = input_update();
        
        // Tasto A (Corto o Lungo) -> SNOOZE
        if (event == INPUT_BTN_A_SHORT || event == INPUT_BTN_A_LONG) {
            Serial.println("Azione: SNOOZE!");
            isSnoozed = true;
            break;
        }
        
        // Tasto B (Corto o Lungo) -> STOP
        if (event == INPUT_BTN_B_SHORT || event == INPUT_BTN_B_LONG) {
            Serial.println("Azione: STOP!");
            break;
        }
        
        // Auto-stop per inattività o hard timeout
        if (nowUs - alarmStartTimeUs > autoStopUs) {
            Serial.println("Azione: AUTO-STOP (Tempo scaduto o Hard Timeout)!");
            break;
        }
        
        // Mantiene il buffer I2S costantemente in play solo se audio è attivo
        if (audioStarted && !loopAudio()) {
            // Se la traccia MP3 finisce, riavviala immediatamente per un loop continuo
            audioStarted = playMp3(trackNames[appState.currentTrackIndex % actualNumTracks]);
            if (!audioStarted) {
                Serial.println("[ALARM] Avviso: Errore nel riavvio del loop audio. Continuo solo con animazione LED.");
                appState.audioError = true;
            }
        }
        
        delay(2); // Pausa minima: la decodifica MP3 richiede CPU costante per non balbettare
    }
    
    // Spegni Audio e LED
    stopAudio();
    led_off(true); // Fade sempre attivo
    
    // Ritorna alla schermata principale a fine suoneria
    appState.currentScreen = SCREEN_HOME;

    if (isSnoozed) {
        // Ricalcola l'orario per lo snooze e aggiorna l'RTC
        time_t utc_epoch = rtc.now().unixtime();
        struct tm* local_timeinfo = localtime(&utc_epoch);
        
        int snoozeMins = local_timeinfo->tm_hour * 60 + local_timeinfo->tm_min + appState.snoozeTime;
        int nextH = (snoozeMins / 60) % 24;
        int nextM = snoozeMins % 60;
        
        // Salvo globalmente le informazioni per lo Snooze
        isSnoozeActive = true;
        snoozeTargetHour = nextH;
        snoozeTargetMinute = nextM;

        Serial.printf("Snooze attivato. Prossima suoneria alle %02d:%02d (Locale)\n", nextH, nextM);
        
        // Ritorna a main.cpp per consentire l'aggiornamento UI con il nuovo orario
        // e l'entrata naturale in Light Sleep, evitando un brutale reboot da Deep Sleep.
        return;
    } else {
        isSnoozeActive = false; // Assicura che lo snooze sia resettato in caso di Stop
    }
}

void calculateNextAlarm(SystemState &appState) {
    if (!appState.alarmsGloballyEnabled) {
        Serial.println("Sveglie globalmente disattivate. Nessun allarme inviato all'RTC.");
        isSnoozeActive = false; // Azzera lo snooze se l'utente spegne tutte le sveglie
        rtc.disableAlarm(2);
        rtc.clearAlarm(2);
        appState.nextAlarmFound = false; // Rimuove l'icona della sveglia dalla schermata Home
        return;
    }
    
    DateTime utc_now = rtc.now();
    time_t utc_epoch = utc_now.unixtime();
    struct tm* local_timeinfo = localtime(&utc_epoch);

    // SCUDO: Se c'è uno snooze in corso, bypasso il calcolo e aggiorno la UI
    if (isSnoozeActive) {
        Serial.printf("[ALARM] Snooze in corso. Mantengo Allarme 2 alle %02d:%02d\n", snoozeTargetHour, snoozeTargetMinute);
        appState.nextAlarmFound = true;
        appState.nextAlarmHour = snoozeTargetHour;
        appState.nextAlarmMinute = snoozeTargetMinute;
        
        struct tm target_time = *local_timeinfo;
        target_time.tm_hour = snoozeTargetHour;
        target_time.tm_min = snoozeTargetMinute;
        target_time.tm_sec = 0;
        target_time.tm_isdst = -1; // Forza la rivalutazione DST per il futuro
        
        time_t target_epoch = mktime(&target_time);
        if (target_epoch <= utc_epoch) {
            target_time.tm_mday += 1;
            target_time.tm_isdst = -1;
            target_epoch = mktime(&target_time);
        }
        
        struct tm* utc_target = gmtime(&target_epoch);
        rtc_setupAlarm2(utc_target->tm_hour, utc_target->tm_min);
        return;
    }

    int currentDayOfWeek = local_timeinfo->tm_wday;
    int currentMinsFromMidnight = local_timeinfo->tm_hour * 60 + local_timeinfo->tm_min;
    
    bool found = false;
    uint32_t minFutureMinutes = 0xFFFFFFFF;
    uint8_t nextHour = 0;
    uint8_t nextMinute = 0;
    int bestDayOffset = 0;

    for (uint8_t i = 0; i < appState.numConfiguredAlarms; i++) {
        if (!appState.alarms[i].active) continue;

        Alarm al = appState.alarms[i];
        
        for (int dayOffset = 0; dayOffset < 30; dayOffset++) {
            int targetDayOfWeek = (currentDayOfWeek + dayOffset) % 7;
            
            bool dayMatches = false;
            switch (al.recurrence) {
                case ALARM_SINGLE:
                case ALARM_DAILY:
                    dayMatches = true;
                    break;
                case ALARM_WEEKDAYS:
                    if (targetDayOfWeek >= 1 && targetDayOfWeek <= 5) dayMatches = true;
                    break;
                case ALARM_WEEKENDS:
                    if (targetDayOfWeek == 0 || targetDayOfWeek == 6) dayMatches = true;
                    break;
            }

            if (!dayMatches) continue;

            int alarmMinsFromMidnight = al.hour * 60 + al.minute;
            
            // Se stiamo controllando oggi (dayOffset=0) e l'orario della sveglia è già passato,
            // salta questo giorno e non considerare questa sveglia per oggi.
            if (dayOffset == 0 && alarmMinsFromMidnight <= currentMinsFromMidnight) {
                continue; 
            }
            
            uint32_t futureMinutes = (dayOffset * 24 * 60) + alarmMinsFromMidnight - currentMinsFromMidnight;
            
            if (futureMinutes < minFutureMinutes) {
                minFutureMinutes = futureMinutes;
                nextHour = al.hour;
                nextMinute = al.minute;
                bestDayOffset = dayOffset;
                found = true;
            }
            
            // Per una sveglia singola, non ha senso controllare i giorni successivi.
            // Per le altre, una volta trovato il primo giorno utile, possiamo fermarci.
            if (dayMatches) break;
        }
    }

    if (found) {
        Serial.printf("[ALARM] Prossima sveglia calcolata tra %u minuti (alle %02d:%02d Locale)\n", minFutureMinutes, nextHour, nextMinute);
        appState.nextAlarmFound = true;
        appState.nextAlarmHour = nextHour;
        appState.nextAlarmMinute = nextMinute;
        
        struct tm target_time = *local_timeinfo;
        target_time.tm_hour = nextHour;
        target_time.tm_min = nextMinute;
        target_time.tm_sec = 0;
        target_time.tm_mday += bestDayOffset;
        target_time.tm_isdst = -1; // Forza C a valutare l'ora legale corretta della destinazione!
        
        time_t target_epoch = mktime(&target_time); // mktime normalizza fuso orario e calcola l'UTC
        struct tm* utc_target = gmtime(&target_epoch); // Convertiamo a UTC per darlo in pasto al chip RTC
        
        Serial.printf("[ALARM] Programmato RTC in UTC: %02d:%02d\n", utc_target->tm_hour, utc_target->tm_min);
        rtc_setupAlarm2(utc_target->tm_hour, utc_target->tm_min);
    } else {
        Serial.println("[ALARM] Nessuna sveglia attiva per i prossimi giorni. Disattivo Allarme 2.");
        appState.nextAlarmFound = false;
        rtc.disableAlarm(2);
        rtc.clearAlarm(2);
    }
}