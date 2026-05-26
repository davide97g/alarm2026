#include "menu.h"
#include "../../include/config.h"
#include "../network/network.h"  // Per getWifiErrorMessage
#include "../led/led.h"
#include "../audio/audio.h"
#include "../input/input.h"
#include "../watchdog/watchdog.h"
#include "../state/StateManager.h"
#include "../alarm/alarm.h"

extern bool ledState;  // Da main
extern int64_t lastActivityTime;  // Da main aggiornato a 64-bit

// Funzioni esterne da main
extern void scheduleScreenUpdate();

extern void triggerManualNtpSync(SystemState &state);
extern void toggleBluetooth(bool enable, const char* deviceName);



extern char trackNames[NUM_TRACKS][64];
extern uint8_t actualNumTracks; // Dichiarazione corretta

void handleMenu(SystemState &appState, InputEvent event) {
    if (appState.currentScreen == SCREEN_HOME) {
        if (event == INPUT_BTN_A_SHORT || event == INPUT_BTN_B_SHORT) {
            // Accendi o spegni il LED per vedere l'ora di notte
            ledState = !ledState;
            Serial.printf("Luce LED: %s\n", ledState ? "ACCESA" : "SPENTA");
            if (ledState) {
                led_on(appState.ledBrightness, true); // Usa l'impostazione per il fade in fissa a true
                scheduleScreenUpdate();
            } else {
                led_off(true); // Fade out fisso a true
                scheduleScreenUpdate();
            }
        }
    } else if (appState.currentScreen == SCREEN_MENU) {
        if (event == INPUT_BTN_A_SHORT) {
            // Tasto A: Naviga verticalmente le voci della scheda in loop
            if (appState.menuCursorIndex == 1 && appState.isEditingAlarm) {
                // Se siamo in modalità modifica sveglia, naviga tra Ora -> Minuti -> Ricorrenza -> Cestino
                appState.alarmEditFieldIndex = (appState.alarmEditFieldIndex + 1) % 4;
            } else {
                uint8_t numItems = 0;
                switch(appState.menuCursorIndex) {
                    case 0: numItems = 9; break; // Impostazioni (9 voci)
                    case 1: numItems = appState.numConfiguredAlarms < 16 ? appState.numConfiguredAlarms + 1 : 16; break; // Sveglie dinamiche
                    case 2: numItems = actualNumTracks; break; // Tracce
                    case 3: numItems = 3; break; // Bluetooth (3 Voci)
                }

                if (numItems > 0) {
                    appState.menuItemIndex = (appState.menuItemIndex + 1) % numItems;
                    Serial.printf("[MENU] Navigazione: Cursore=%d ItemIndex=%d\n", appState.menuCursorIndex, appState.menuItemIndex);
                }
            }
        } else if (event == INPUT_BTN_A_LONG) {
            // Tasto A Lungo: Naviga indietro di uno step
            if (appState.menuCursorIndex == 1 && appState.isEditingAlarm) {
                // NUOVO COMANDO: A Lungo in modalità modifica CONFERMA ed ESCE
                appState.isEditingAlarm = false;
            } else {
                uint8_t numItems = 0;
                switch(appState.menuCursorIndex) {
                    case 0: numItems = 9; break;
                    case 1: numItems = appState.numConfiguredAlarms < 16 ? appState.numConfiguredAlarms + 1 : 16; break;
                    case 2: numItems = actualNumTracks; break;
                    case 3: numItems = 3; break;
                }

                if (numItems > 0) {
                    appState.menuItemIndex = (appState.menuItemIndex + numItems - 1) % numItems;
                    Serial.printf("[MENU] Navigazione indietro: Cursore=%d ItemIndex=%d\n", appState.menuCursorIndex, appState.menuItemIndex);
                }
            }
        } else if (event == INPUT_BTN_B_SHORT || (event == INPUT_BTN_B_LONG && appState.menuCursorIndex == 2)) {
            // Tasto B: Naviga le opzioni (nella scheda 2, B_LONG funge anch'esso da PLAY/STOP)
            // Tasto B: Naviga le opzioni della singola voce del menù in loop
            if (appState.menuCursorIndex == 0) { // Impostazioni                
                switch (appState.menuItemIndex) {
                    case 0: // Sveglie Globali (ON/OFF)
                        appState.alarmsGloballyEnabled = !appState.alarmsGloballyEnabled;
                        stateManager.setAlarmsGloballyEnabled(appState, appState.alarmsGloballyEnabled);
                        calculateNextAlarm(appState); // Ricalcola per aggiornare o svuotare l'RTC
                        break;
                    case 1: // Volume Sveglia (1-10)
                        appState.alarmVolume = (appState.alarmVolume % 10) + 1;
                        stateManager.setAlarmVolume(appState, appState.alarmVolume);
                        break;
                    case 2: // Snooze Time (1-20)
                        appState.snoozeTime = (appState.snoozeTime % 20) + 1;
                        stateManager.setSnoozeTime(appState, appState.snoozeTime);
                        break;
                    case 3: // Auto-Stop Time (1-5)
                        appState.autoStopTime = (appState.autoStopTime % 5) + 1;
                        stateManager.setAutoStopTime(appState, appState.autoStopTime);
                        break;
                    case 4: // Sincronizzazione NTP automatica (ON/OFF)
                        appState.ntpDisabled = !appState.ntpDisabled;
                        stateManager.setNtpDisabled(appState, appState.ntpDisabled);
                        Serial.printf("[MENU] Sincronizzazione NTP: %s\n", appState.ntpDisabled ? "DISABILITATA" : "ABILITATA");
                        if (appState.ntpDisabled) {
                            strncpy(appState.wifiStatus, "✗ Disabilit.", sizeof(appState.wifiStatus) - 1);
                        } else {
                            strncpy(appState.wifiStatus, "↻ In attesa", sizeof(appState.wifiStatus) - 1);
                        }
                        break;
                    case 5: // Forza Sincro NTP
                        if (!appState.ntpDisabled) {
                            triggerManualNtpSync(appState);
                        } else {
                            strncpy(appState.wifiStatus, "✗ Abilita prima NTP", sizeof(appState.wifiStatus) - 1);
                        }
                        break;
                    case 6: // Luminosità LED (Spostato qui)
                        appState.ledBrightness = (appState.ledBrightness + 1) % 11;
                        stateManager.setLedBrightness(appState, appState.ledBrightness);
                        if (ledState) led_on(appState.ledBrightness, false); // Variazione istantanea nel menù
                        break;
                    case 7: // Auto-Spegnimento LED (Spostato qui)
                        appState.ledAutoOffTime = (appState.ledAutoOffTime >= 30) ? 5 : appState.ledAutoOffTime + 5;
                        stateManager.setLedAutoOffTime(appState, appState.ledAutoOffTime);
                        break;
                    case 8: // Font UI
                        appState.selectedFontIndex = (appState.selectedFontIndex + 1) % 3;
                        stateManager.setFontIndex(appState, appState.selectedFontIndex);
                        Serial.printf("[MENU] Font selezionato: %d\n", appState.selectedFontIndex);
                        break;
                }
            } else if (appState.menuCursorIndex == 2) { // Tracce
                // 1. Salva l'indice della traccia selezionata istantaneamente
                appState.currentTrackIndex = appState.menuItemIndex;
                stateManager.setTrackIndex(appState, appState.currentTrackIndex);

                // 2. Imposta l'UI in modalità "Riproduzione" (mostrerà il quadratino STOP)
                appState.isPreviewPlaying = true;
                appState.audioError = false;
                ui_update(appState);

                // 3. Avvia la riproduzione audio bloccante
                if (actualNumTracks > 0) {
                    setVolume(appState.alarmVolume);
                    bool audioStarted = playMp3(trackNames[appState.currentTrackIndex % actualNumTracks]);
                    
                    if (!audioStarted) {
                        Serial.println("[MENU] Errore: impossibile riprodurre l'anteprima.");
                        appState.audioError = true;
                    } else {
                        Serial.println("[MENU] Anteprima in corso. Premi un tasto (es. B) per fermare.");
                        
                        int64_t previewStartUs = esp_timer_get_time();
                        
                        while (true) {
                            watchdog_feed();
                            
                            if (!loopAudio()) {
                                break; // Traccia finita naturalmente
                            }

                            // Qualsiasi tasto (incluso B corto o B lungo) intercetterà e fermerà
                            InputEvent stopEvent = input_get_event(0);
                            if (stopEvent != INPUT_NONE) {
                                Serial.println("[MENU] Anteprima interrotta dall'utente.");
                                break;
                            }

                            // Timeout di sicurezza di 15 secondi (evita riproduzioni infinite nei menù)
                            if (esp_timer_get_time() - previewStartUs > 15000000LL) {
                                Serial.println("[MENU] Timeout anteprima raggiunto.");
                                break;
                            }

                            delay(2);
                        }
                        
                        stopAudio();
                    }
                } else {
                    appState.audioError = true;
                }

                // 4. Fine anteprima: ripristina UI
                appState.isPreviewPlaying = false;
                ui_update(appState);
                lastActivityTime = esp_timer_get_time(); // Resetta lo sleep per non spegnere subito il display
            } else if (appState.menuCursorIndex == 3) { // Bluetooth
                switch (appState.menuItemIndex) {
                    case 0: // Abilita Bluetooth
                        appState.bluetoothEnabled = !appState.bluetoothEnabled;
                        stateManager.setBluetoothEnabled(appState, appState.bluetoothEnabled);
                        toggleBluetooth(appState.bluetoothEnabled, "Sveglia Smart");
                        break;
                    case 1: // Dispositivi associati
                        // TODO: Implementare visualizzazione lista (Richiede UI aggiuntiva)
                        Serial.println("[BLUETOOTH] Richiesta visualizzazione dispositivi associati");
                        break;
                    case 2: // Dimentica dispositivi (Hint: usa press lungo)
                        Serial.println("[BLUETOOTH] Tieni premuto B per confermare l'eliminazione");
                        break;
                }
            } else if (appState.menuCursorIndex == 1) { // Sveglie
                if (appState.isEditingAlarm) {
                    uint8_t idx = appState.menuItemIndex;
                    if (appState.alarmEditFieldIndex == 3) {
                        // Elimina la sveglia
                        appState.isSnoozeActive = false; // Cancella eventuali snooze in corso
                        stateManager.deleteAlarm(appState, idx);
                        appState.isEditingAlarm = false;
                        appState.alarmEditFieldIndex = 0;
                    } else {
                        if (appState.alarmEditFieldIndex == 0) {
                            appState.alarms[idx].hour = (appState.alarms[idx].hour + 1) % 24;
                        } else if (appState.alarmEditFieldIndex == 1) {
                            appState.alarms[idx].minute = (appState.alarms[idx].minute + 5) % 60;
                        } else if (appState.alarmEditFieldIndex == 2) {
                            appState.alarms[idx].recurrence = (AlarmRecurrence)((appState.alarms[idx].recurrence + 1) % 4);
                        }
                        stateManager.saveAllAlarms(appState);
                        appState.isSnoozeActive = false; // Cancella snooze fantasma se l'utente agisce
                        calculateNextAlarm(appState);
                    }
                } else {
                    uint8_t idx = appState.menuItemIndex;
                    if (idx == appState.numConfiguredAlarms && idx < 16) {
                        // ADD ALARM
                        appState.numConfiguredAlarms++;
                        appState.alarms[idx].hour = 7;
                        appState.alarms[idx].minute = 0;
                        appState.alarms[idx].active = true;
                        appState.alarms[idx].recurrence = ALARM_DAILY;
                        appState.isEditingAlarm = true;
                        appState.alarmEditFieldIndex = 0;
                        stateManager.saveAllAlarms(appState);
                        appState.isSnoozeActive = false;
                        calculateNextAlarm(appState);
                    } else {
                        appState.alarms[idx].active = !appState.alarms[idx].active;
                        stateManager.saveAllAlarms(appState);
                        appState.isSnoozeActive = false;
                        calculateNextAlarm(appState);
                    }
                }
            }
        }
    }

    if (event == INPUT_BTN_AB_SHORT) {
        if (appState.currentScreen == SCREEN_MENU) {
            appState.isEditingAlarm = false;
            appState.menuCursorIndex = (appState.menuCursorIndex + 1) % 4;
            appState.menuItemIndex = (appState.menuCursorIndex == 2) ? (appState.currentTrackIndex % (actualNumTracks > 0 ? actualNumTracks : 1)) : 0;
        }
    }

    if (event == INPUT_BTN_B_LONG) {
        if (appState.currentScreen == SCREEN_MENU) {
            if (appState.menuCursorIndex == 0) { // Impostazioni                
                switch (appState.menuItemIndex) {
                    case 0: // Sveglie Globali (ON/OFF)
                        appState.alarmsGloballyEnabled = !appState.alarmsGloballyEnabled;
                        stateManager.setAlarmsGloballyEnabled(appState, appState.alarmsGloballyEnabled);
                        calculateNextAlarm(appState); 
                        break;
                    case 1: // Volume Sveglia (1-10) scorrimento indietro
                        appState.alarmVolume = (appState.alarmVolume <= 1) ? 10 : appState.alarmVolume - 1;
                        stateManager.setAlarmVolume(appState, appState.alarmVolume);
                        break;
                    case 2: // Snooze Time (1-20) scorrimento indietro
                        appState.snoozeTime = (appState.snoozeTime <= 1) ? 20 : appState.snoozeTime - 1;
                        stateManager.setSnoozeTime(appState, appState.snoozeTime);
                        break;
                    case 3: // Auto-Stop Time (1-5) scorrimento indietro
                        appState.autoStopTime = (appState.autoStopTime <= 1) ? 5 : appState.autoStopTime - 1;
                        stateManager.setAutoStopTime(appState, appState.autoStopTime);
                        break;
                    case 4: // Sincronizzazione NTP automatica (ON/OFF)
                        appState.ntpDisabled = !appState.ntpDisabled;
                        stateManager.setNtpDisabled(appState, appState.ntpDisabled);
                        Serial.printf("[MENU] Sincronizzazione NTP: %s\n", appState.ntpDisabled ? "DISABILITATA" : "ABILITATA");
                        if (appState.ntpDisabled) {
                            strncpy(appState.wifiStatus, "✗ Disabilit.", sizeof(appState.wifiStatus) - 1);
                        } else {
                            strncpy(appState.wifiStatus, "↻ In attesa", sizeof(appState.wifiStatus) - 1);
                        }
                        break;
                    case 5: // Forza Sincro NTP
                        if (!appState.ntpDisabled) {
                            triggerManualNtpSync(appState);
                        } else {
                            strncpy(appState.wifiStatus, "✗ Abilita prima NTP", sizeof(appState.wifiStatus) - 1);
                        }
                        break;
                    case 6: // Luminosità LED (0-10) scorrimento indietro
                        appState.ledBrightness = (appState.ledBrightness == 0) ? 10 : appState.ledBrightness - 1;
                        stateManager.setLedBrightness(appState, appState.ledBrightness);
                        if (ledState) led_on(appState.ledBrightness, false); 
                        break;
                    case 7: // Auto-Spegnimento LED scorrimento indietro
                        appState.ledAutoOffTime = (appState.ledAutoOffTime <= 5) ? 30 : appState.ledAutoOffTime - 5;
                        stateManager.setLedAutoOffTime(appState, appState.ledAutoOffTime);
                        break;
                    case 8: // Font UI
                        appState.selectedFontIndex = (appState.selectedFontIndex + 2) % 3; // Corrisponde a -1 su base 3
                        stateManager.setFontIndex(appState, appState.selectedFontIndex);
                        Serial.printf("[MENU] Font selezionato: %d\n", appState.selectedFontIndex);
                        break;
                }
            } else if (appState.menuCursorIndex == 1) { // Sveglie
                if (appState.isEditingAlarm) {
                    // NUOVO COMANDO: B Lungo in modalità modifica SCORRE INDIETRO i valori
                    uint8_t idx = appState.menuItemIndex;
                    if (appState.alarmEditFieldIndex == 3) {
                        // Elimina la sveglia anche con pressione lunga per coerenza
                        appState.isSnoozeActive = false; // Cancella eventuali snooze in corso
                        stateManager.deleteAlarm(appState, idx);
                        appState.isEditingAlarm = false;
                        appState.alarmEditFieldIndex = 0;
                    } else {
                        if (appState.alarmEditFieldIndex == 0) {
                            appState.alarms[idx].hour = (appState.alarms[idx].hour + 23) % 24; // -1 ora
                        } else if (appState.alarmEditFieldIndex == 1) {
                            appState.alarms[idx].minute = (appState.alarms[idx].minute + 55) % 60; // -5 minuti
                        } else if (appState.alarmEditFieldIndex == 2) {
                            appState.alarms[idx].recurrence = (AlarmRecurrence)((appState.alarms[idx].recurrence + 3) % 4); // Ricorrenza precedente
                        }
                        stateManager.saveAllAlarms(appState);
                        appState.isSnoozeActive = false; // Cancella snooze fantasma anche se si scorre indietro
                        calculateNextAlarm(appState);
                    }
                } else {
                    // Se non stiamo modificando, B Lungo ENTRA in modalità modifica
                    uint8_t idx = appState.menuItemIndex;
                    if (idx == appState.numConfiguredAlarms && idx < 16) {
                        // ADD ALARM anche con pressione lunga
                        appState.numConfiguredAlarms++;
                        appState.alarms[idx].hour = 7;
                        appState.alarms[idx].minute = 0;
                        appState.alarms[idx].active = true;
                        appState.alarms[idx].recurrence = ALARM_DAILY;
                        stateManager.saveAllAlarms(appState);
                        appState.isSnoozeActive = false;
                        calculateNextAlarm(appState);
                    }
                    appState.isEditingAlarm = true;
                    appState.alarmEditFieldIndex = 0;
                }
            }
        } else if (appState.menuCursorIndex == 3) { // Bluetooth
            if (appState.menuItemIndex == 2) {
                // Dimentica dispositivi
                Serial.println("[BLUETOOTH] Cancellazione memoria dispositivi...");
                appState.pairedDevicesCount = 0;
                // TODO: Implementare svuotamento effettivo memoria ESP_A2D
            }
        }
    }

    if (event == INPUT_BTN_AB_LONG) {
        if (appState.currentScreen == SCREEN_HOME) {
            appState.currentScreen = SCREEN_MENU;
        } else if (appState.currentScreen == SCREEN_MENU) {
            // Esce dal menù e torna alla schermata Home
            appState.currentScreen = SCREEN_HOME;
            appState.isEditingAlarm = false; // Sicurezza: esce dalla modalità modifica sveglia se era rimasta attiva
        }
    }
}