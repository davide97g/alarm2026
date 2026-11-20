#include "menu.h"
#include "../../include/config.h"
#include "../led/led.h"
#include "../audio/audio.h"
#include "../input/input.h"
#include "../watchdog/watchdog.h"
#include "../state/StateManager.h"
#include "../alarm/alarm.h"
#include "../logic/logic.h"

extern bool ledState;            // main.cpp
extern int64_t lastActivityTime; // main.cpp
extern void scheduleScreenUpdate();
extern void triggerManualNtpSync(SystemState &state);

extern char trackNames[NUM_TRACKS][64];
extern uint8_t actualNumTracks;

// Tasto A: riga successiva (dir=+1, corto) o precedente (dir=-1, lungo).
// In modifica sveglia: A corto cambia campo, A lungo conferma ed esce.
static void navigate(SystemState &s, int dir) {
    if (s.menuCursorIndex == 1 && s.isEditingAlarm) {
        if (dir > 0) s.alarmEditFieldIndex = (s.alarmEditFieldIndex + 1) % 4; // Ora -> Minuti -> Ricorrenza -> Cestino
        else s.isEditingAlarm = false;
        return;
    }
    uint8_t numItems = menuItemCount(s, actualNumTracks);
    if (numItems == 0) return;
    if (dir > 0) {
        s.menuItemIndex = (s.menuItemIndex + 1) % numItems;
        Serial.printf("[MENU] Navigazione: Cursore=%d ItemIndex=%d\n", s.menuCursorIndex, s.menuItemIndex);
    } else {
        s.menuItemIndex = (s.menuItemIndex + numItems - 1) % numItems;
        Serial.printf("[MENU] Navigazione indietro: Cursore=%d ItemIndex=%d\n", s.menuCursorIndex, s.menuItemIndex);
    }
}

// Scheda Impostazioni: B corto avanza il valore (fwd), B lungo lo fa tornare indietro
static void changeSetting(SystemState &s, bool fwd) {
    switch (s.menuItemIndex) {
        case 0: // Sveglie globali ON/OFF
            s.alarmsGloballyEnabled = !s.alarmsGloballyEnabled;
            stateManager.setAlarmsGloballyEnabled(s, s.alarmsGloballyEnabled);
            calculateNextAlarm(s); // Aggiorna o svuota l'RTC
            break;
        case 1: // Volume 1-10
            s.alarmVolume = fwd ? (s.alarmVolume % 10) + 1 : (s.alarmVolume <= 1 ? 10 : s.alarmVolume - 1);
            stateManager.setAlarmVolume(s, s.alarmVolume);
            break;
        case 2: // Snooze 1-20 min
            s.snoozeTime = fwd ? (s.snoozeTime % 20) + 1 : (s.snoozeTime <= 1 ? 20 : s.snoozeTime - 1);
            stateManager.setSnoozeTime(s, s.snoozeTime);
            break;
        case 3: // Auto-Stop 1-5 min
            s.autoStopTime = fwd ? (s.autoStopTime % 5) + 1 : (s.autoStopTime <= 1 ? 5 : s.autoStopTime - 1);
            stateManager.setAutoStopTime(s, s.autoStopTime);
            break;
        case 4: // Sincronizzazione NTP automatica ON/OFF
            s.ntpDisabled = !s.ntpDisabled;
            stateManager.setNtpDisabled(s, s.ntpDisabled);
            Serial.printf("[MENU] Sincronizzazione NTP: %s\n", s.ntpDisabled ? "DISABILITATA" : "ABILITATA");
            strncpy(s.wifiStatus, s.ntpDisabled ? "✗ Disabilit." : "↻ In attesa", sizeof(s.wifiStatus) - 1);
            break;
        case 5: // Forza sincro NTP
            if (!s.ntpDisabled) triggerManualNtpSync(s);
            else strncpy(s.wifiStatus, "✗ Abilita prima NTP", sizeof(s.wifiStatus) - 1);
            break;
        case 6: // Luminosità LED 0-10
            s.ledBrightness = fwd ? (s.ledBrightness + 1) % 11 : (s.ledBrightness == 0 ? 10 : s.ledBrightness - 1);
            stateManager.setLedBrightness(s, s.ledBrightness);
            if (ledState) led_on(s.ledBrightness, false); // Variazione istantanea
            break;
        case 7: // Auto-spegnimento LED 5-30 s
            s.ledAutoOffTime = fwd ? (s.ledAutoOffTime >= 30 ? 5 : s.ledAutoOffTime + 5)
                                   : (s.ledAutoOffTime <= 5 ? 30 : s.ledAutoOffTime - 5);
            stateManager.setLedAutoOffTime(s, s.ledAutoOffTime);
            break;
        case 8: // Font UI (3 font)
            s.selectedFontIndex = (s.selectedFontIndex + (fwd ? 1 : 2)) % 3;
            stateManager.setFontIndex(s, s.selectedFontIndex);
            Serial.printf("[MENU] Font selezionato: %d\n", s.selectedFontIndex);
            break;
    }
}

// Scheda Tracce: seleziona la traccia e ne suona un'anteprima (bloccante, max 15 s)
static void previewTrack(SystemState &s) {
    s.currentTrackIndex = s.menuItemIndex;
    stateManager.setTrackIndex(s, s.currentTrackIndex);

    s.isPreviewPlaying = true; // La UI mostra il quadratino STOP
    s.audioError = false;
    ui_update(s);

    if (actualNumTracks > 0) {
        setVolume(s.alarmVolume);
        if (!playMp3(trackNames[s.currentTrackIndex % actualNumTracks])) {
            Serial.println("[MENU] Errore: impossibile riprodurre l'anteprima.");
            s.audioError = true;
        } else {
            Serial.println("[MENU] Anteprima in corso. Premi un tasto (es. B) per fermare.");
            int64_t previewStartUs = esp_timer_get_time();
            while (true) {
                watchdog_feed();
                if (!loopAudio()) break; // Traccia finita
                if (input_get_event(0) != INPUT_NONE) { // Qualsiasi tasto ferma
                    Serial.println("[MENU] Anteprima interrotta dall'utente.");
                    break;
                }
                if (esp_timer_get_time() - previewStartUs > 15000000LL) {
                    Serial.println("[MENU] Timeout anteprima raggiunto.");
                    break;
                }
                delay(2);
            }
            stopAudio();
        }
    } else {
        s.audioError = true;
    }

    s.isPreviewPlaying = false;
    ui_update(s);
    lastActivityTime = esp_timer_get_time(); // Non spegnere subito il display
}

// Ogni modifica alle sveglie: salva, annulla snooze fantasma, riprogramma l'RTC
static void commitAlarms(SystemState &s) {
    stateManager.saveAllAlarms(s);
    s.isSnoozeActive = false;
    calculateNextAlarm(s);
}

// Nuova sveglia alle 07:00 tutti i giorni nello slot idx
static void addAlarm(SystemState &s, uint8_t idx) {
    s.numConfiguredAlarms++;
    s.alarms[idx] = {7, 0, true, ALARM_DAILY};
}

// In modifica: B corto (fwd) aumenta il campo, B lungo lo diminuisce; sul cestino elimina
static void editAlarm(SystemState &s, bool fwd) {
    uint8_t idx = s.menuItemIndex;
    Alarm &al = s.alarms[idx];
    switch (s.alarmEditFieldIndex) {
        case 3:
            s.isSnoozeActive = false;
            stateManager.deleteAlarm(s, idx);
            s.isEditingAlarm = false;
            s.alarmEditFieldIndex = 0;
            return;
        case 0: al.hour = (al.hour + (fwd ? 1 : 23)) % 24; break;
        case 1: al.minute = (al.minute + (fwd ? 5 : 55)) % 60; break;
        case 2: al.recurrence = (AlarmRecurrence)((al.recurrence + (fwd ? 1 : 3)) % 4); break;
    }
    commitAlarms(s);
}

// Scheda Bluetooth, B corto
static void bluetoothAction(SystemState &s) {
    switch (s.menuItemIndex) {
        case 0:
            s.bluetoothEnabled = !s.bluetoothEnabled;
            stateManager.setBluetoothEnabled(s, s.bluetoothEnabled);
            toggleBluetooth(s.bluetoothEnabled, "Sveglia Smart");
            break;
        case 1: // TODO: lista dispositivi (richiede UI aggiuntiva)
            Serial.println("[BLUETOOTH] Richiesta visualizzazione dispositivi associati");
            break;
        case 2:
            Serial.println("[BLUETOOTH] Tieni premuto B per confermare l'eliminazione");
            break;
    }
}

// Riga "+" in fondo alla lista sveglie (se c'è ancora uno slot libero)
static bool onAddRow(const SystemState &s) {
    return s.menuItemIndex == s.numConfiguredAlarms && s.menuItemIndex < 16;
}

void handleMenu(SystemState &s, InputEvent event) {
    if (s.currentScreen == SCREEN_HOME) {
        if (event == INPUT_BTN_A_SHORT || event == INPUT_BTN_B_SHORT) {
            // Luce notturna on/off
            ledState = !ledState;
            Serial.printf("Luce LED: %s\n", ledState ? "ACCESA" : "SPENTA");
            if (ledState) led_on(s.ledBrightness, true);
            else led_off(true);
            scheduleScreenUpdate();
        }
    } else if (s.currentScreen == SCREEN_MENU) {
        if (event == INPUT_BTN_A_SHORT) {
            navigate(s, +1);
        } else if (event == INPUT_BTN_A_LONG) {
            navigate(s, -1);
        } else if (event == INPUT_BTN_B_SHORT || (event == INPUT_BTN_B_LONG && s.menuCursorIndex == 2)) {
            // B corto agisce sulla riga; nelle Tracce anche B lungo avvia/ferma l'anteprima
            switch (s.menuCursorIndex) {
                case 0: changeSetting(s, true); break;
                case 2: previewTrack(s); break;
                case 3: bluetoothAction(s); break;
                case 1:
                    if (s.isEditingAlarm) {
                        editAlarm(s, true);
                    } else if (onAddRow(s)) {
                        addAlarm(s, s.menuItemIndex);
                        s.isEditingAlarm = true;
                        s.alarmEditFieldIndex = 0;
                        commitAlarms(s);
                    } else {
                        s.alarms[s.menuItemIndex].active = !s.alarms[s.menuItemIndex].active;
                        commitAlarms(s);
                    }
                    break;
            }
        }
    }

    if (event == INPUT_BTN_AB_SHORT && s.currentScreen == SCREEN_MENU) {
        // A+B corto: scheda successiva (sulle Tracce evidenzia quella selezionata)
        s.isEditingAlarm = false;
        s.menuCursorIndex = (s.menuCursorIndex + 1) % 4;
        s.menuItemIndex = (s.menuCursorIndex == 2) ? (s.currentTrackIndex % (actualNumTracks > 0 ? actualNumTracks : 1)) : 0;
    }

    if (event == INPUT_BTN_B_LONG) {
        if (s.currentScreen == SCREEN_MENU) {
            if (s.menuCursorIndex == 0) {
                changeSetting(s, false);
            } else if (s.menuCursorIndex == 1) {
                if (s.isEditingAlarm) {
                    editAlarm(s, false);
                } else {
                    // B lungo entra in modifica (sulla riga "+" crea prima la sveglia)
                    if (onAddRow(s)) {
                        addAlarm(s, s.menuItemIndex);
                        commitAlarms(s);
                    }
                    s.isEditingAlarm = true;
                    s.alarmEditFieldIndex = 0;
                }
            }
        } else if (s.menuCursorIndex == 3 && s.menuItemIndex == 2) {
            // NB: comportamento originale conservato. Questo ramo scatta FUORI dal menù
            // (es. B lungo in Home con l'ultima scheda aperta = Bluetooth/riga 2).
            Serial.println("[BLUETOOTH] Cancellazione memoria dispositivi...");
            s.pairedDevicesCount = 0;
            // TODO: svuotamento effettivo memoria ESP_A2D
        }
    }

    if (event == INPUT_BTN_AB_LONG) {
        // A+B lungo: entra/esce dal menù
        if (s.currentScreen == SCREEN_HOME) {
            s.currentScreen = SCREEN_MENU;
        } else if (s.currentScreen == SCREEN_MENU) {
            s.currentScreen = SCREEN_HOME;
            s.isEditingAlarm = false;
        }
    }
}
