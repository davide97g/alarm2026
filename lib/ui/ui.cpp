#include "ui.h"
#include "display.h" 
#include "../watchdog/watchdog.h"
#include <stdio.h>
#include <math.h>

#include <Arduino.h>
#include "../../include/config.h"
#include <time.h>
// Inclusione dei font predefiniti di Adafruit_GFX
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
// Aggiungi qui gli include per font personalizzati, es. #include "fonts/Arial12pt.h"
#include <esp_attr.h>
#include "../fonts/icone.h" // Inclusione del nostro nuovo file di icone

// --- INCLUSIONE FONT ---
#include "../fonts/Futura_Medium_alphanum_menu16pt7b.h" // Sostituisce il 20pt
#include "../fonts/Futura_Medium_alphanum_menu8pt7b.h"
#include "../fonts/Futura_Medium_onlynumbers112pt7b.h"

#include "../audio/audio.h" // Per accedere all'array trackWaveforms

const GFXfont* fonts[] = {
    &FreeSans9pt7b,                       // 0: Font di default ESP32
    &Futura_Medium_alphanum_menu16pt7b,   // 1: Futura 16pt (leggibile)
    &Futura_Medium_alphanum_menu8pt7b     // 2: Futura 8pt (piccolo)
};
const uint8_t NUM_FONTS = sizeof(fonts) / sizeof(fonts[0]);

extern char trackNames[NUM_TRACKS][64];
extern uint8_t actualNumTracks;

// Valori iniziali fittizi (force refresh al primo giro)
RTC_DATA_ATTR SystemState lastDrawnState = {
    /* .hour, .minute, .day, .month, .year, .dayOfWeek */
    255, 255, 0, 0, 0, 8,
    /* .temperature, .humidity, .batteryVoltage, .batteryPercent, .batAdcMin, .batAdcMax, .longPressMs */
    -999.0, -999.0, 0.0, 255, 0, 0, 0,
    /* .currentScreen, .menuCursorIndex, .menuItemIndex, .currentTrackIndex, .isEditingAlarm, .alarmEditFieldIndex, .nextAlarmFound, .nextAlarmHour, .nextAlarmMinute */
    SCREEN_HOME, 255, 255, 255, false, 255, false, 255, 255,
    /* .alarmVolume, .snoozeTime, .autoStopTime, .alarmsGloballyEnabled */
    255, 255, 255, false,
    /* .alarms */
    {
        {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE},
        {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE},
        {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE},
        {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}, {255, 255, false, ALARM_SINGLE}    },
    /* .numConfiguredAlarms, .ledBrightness, .ledAutoOffTime, .selectedFontIndex, .isPreviewPlaying, .audioError */
    255, 255, 255, 255, false, false,
    /* .ntpSynced, .ntpDisabled, .wifiStatus, .lastNtpSyncTime */
    false, false, "", 0,
    /* .bluetoothEnabled, .bluetoothConnected, .bluetoothDeviceName, .pairedDevicesCount */
    false, false, "", 0
};

// Voci del menù per le Tab orizzontali (abbreviate per farle entrare in ~198px)
const char* tabItems[] = {
    "Impostazioni",
    "Sveglie",
    "Traccia audio",
    "Bluetooth"
};
const uint8_t NUM_TAB_ITEMS = 4;

// Funzioni di disegno interne
void drawHomeScreen(const SystemState& state);
void drawMenuScreen(const SystemState& state);
void drawAlarmScreen(const SystemState& state);

// --- Funzioni helper per disegnare icone semplici ---
static void drawAlarmIcon(int16_t x, int16_t y, bool active, uint16_t color) {
    const unsigned char* iconToDraw = active ? icon_alarm_40x40 : icon_alarm_off_40x40;
    display.drawBitmap(x, y, iconToDraw, 40, 40, GxEPD_WHITE, color);
}

static void drawBatteryIcon(int16_t x, int16_t y, uint8_t percentage, uint16_t color) {
    const unsigned char* iconToDraw;
    
    if (percentage >= 95)      iconToDraw = icon_battery_android_full_24x24;
    else if (percentage >= 80) iconToDraw = icon_battery_android_frame_6_24x24;
    else if (percentage >= 60) iconToDraw = icon_battery_android_frame_5_24x24;
    else if (percentage >= 40) iconToDraw = icon_battery_android_frame_4_24x24;
    else if (percentage >= 20) iconToDraw = icon_battery_android_frame_3_24x24;
    else if (percentage >= 10) iconToDraw = icon_battery_android_frame_2_24x24;
    else if (percentage >= 5)  iconToDraw = icon_battery_android_frame_1_24x24;
    else                       iconToDraw = icon_battery_android_frame_alert_24x24;
    
    display.drawBitmap(x, y, iconToDraw, 24, 24, GxEPD_WHITE, color);
}

static void drawThermometerIcon(int16_t x, int16_t y, uint16_t color) {
    display.drawBitmap(x, y, icon_thermometer_40x40, 40, 40, GxEPD_WHITE, color);
}

static void drawHumidityIcon(int16_t x, int16_t y, uint16_t color) {
    display.drawBitmap(x, y, icon_humidity_percentage_40x40, 40, 40, GxEPD_WHITE, color);
}

static void drawWifiIcon(int16_t x, int16_t y, uint16_t color) {
    // Disegna l'icona bitmap WiFi 24x24 passata dalla memoria Flash (PROGMEM)
    // L'icona è mappata 1 a 1 sui pixel (24 pixel larghezza x 24 altezza)
    display.drawBitmap(x, y, icon_wifi_24x24, 24, 24, GxEPD_WHITE, color);
}

static void drawWifiErrorIcon(int16_t x, int16_t y, uint16_t color) {
    // Wifi barrato: onde con linea diagonale
    drawWifiIcon(x, y, color);
    display.drawLine(x + 2, y + 2, x + 18, y + 18, color);
}

// Helper per disegnare uno switch ON/OFF usando le icone bitmap Material
static void drawToggleSwitch(int16_t x, int16_t y, bool isOn, bool isSelected) {
    uint16_t bgColor = isSelected ? GxEPD_BLACK : GxEPD_WHITE;
    uint16_t fgColor = isSelected ? GxEPD_WHITE : GxEPD_BLACK;
    
    const unsigned char* icon = isOn ? icon_toggle_on_24x24 : icon_toggle_off_24x24;
    
    // Usa drawBitmap con sfondo opaco: i bit a 1 (sfondo) diventano bgColor, i bit a 0 (icona) diventano fgColor
    display.drawBitmap(x, y, icon, 24, 24, bgColor, fgColor);
}


void ui_init() {
    Serial.println("[UI] Sistema Front-end inizializzato.");
}

void ui_update(const SystemState& newState, bool forceFullRefresh) {
    bool needsUpdate = forceFullRefresh;
    bool exitingMenu = false;

    if (newState.currentScreen != lastDrawnState.currentScreen) {
        needsUpdate = true;
        if (lastDrawnState.currentScreen == SCREEN_MENU && newState.currentScreen == SCREEN_HOME) {
            exitingMenu = true;
        }
        Serial.printf("[UI] Cambio schermata: %s -> %s\n", 
                     lastDrawnState.currentScreen == SCREEN_HOME ? "HOME" : "MENU",
                     newState.currentScreen == SCREEN_HOME ? "HOME" : "MENU");
    }

        // Aggiorna se cambia il minuto, temperatura, o batteria (sulla HOME)
        if (newState.currentScreen == SCREEN_HOME) {
            if (newState.minute != lastDrawnState.minute ||
                abs(newState.temperature - lastDrawnState.temperature) >= 0.5 ||
                newState.batteryPercent != lastDrawnState.batteryPercent ||
                newState.day != lastDrawnState.day || // Aggiorna se cambia il giorno
                newState.nextAlarmFound != lastDrawnState.nextAlarmFound || // Aggiorna se una sveglia viene attivata/disattivata
                newState.nextAlarmHour != lastDrawnState.nextAlarmHour ||
                newState.nextAlarmMinute != lastDrawnState.nextAlarmMinute ||
                newState.ntpSynced != lastDrawnState.ntpSynced) { // Aggiorna se cambia stato icona WiFi
                needsUpdate = true;
            }
        }
        // Aggiorna se l'utente si muove su/giù nel menù
        else if (newState.currentScreen == SCREEN_MENU) {
            bool alarmsChanged = false;
            for (int i = 0; i < 16; i++) {
                if (newState.alarms[i].hour != lastDrawnState.alarms[i].hour ||
                    newState.alarms[i].minute != lastDrawnState.alarms[i].minute ||
                    newState.alarms[i].active != lastDrawnState.alarms[i].active ||
                    newState.alarms[i].recurrence != lastDrawnState.alarms[i].recurrence) {
                    alarmsChanged = true;
                    break;
                }
            }

            if (newState.menuCursorIndex != lastDrawnState.menuCursorIndex ||
                newState.menuItemIndex != lastDrawnState.menuItemIndex ||
                newState.isEditingAlarm != lastDrawnState.isEditingAlarm ||
                newState.alarmEditFieldIndex != lastDrawnState.alarmEditFieldIndex ||
                // Aggiungiamo il controllo sui valori per ridisegnare quando vengono modificati
                newState.numConfiguredAlarms != lastDrawnState.numConfiguredAlarms ||
                newState.alarmVolume != lastDrawnState.alarmVolume ||
                newState.snoozeTime != lastDrawnState.snoozeTime ||
                newState.autoStopTime != lastDrawnState.autoStopTime ||
                newState.alarmsGloballyEnabled != lastDrawnState.alarmsGloballyEnabled ||
                newState.ledBrightness != lastDrawnState.ledBrightness ||
                newState.ledAutoOffTime != lastDrawnState.ledAutoOffTime ||
                newState.audioError != lastDrawnState.audioError ||
                newState.isPreviewPlaying != lastDrawnState.isPreviewPlaying ||
                abs(newState.batteryVoltage - lastDrawnState.batteryVoltage) > 0.01 || // Aggiunto per aggiornare il voltaggio
                newState.currentTrackIndex != lastDrawnState.currentTrackIndex || // Mancava!
                newState.selectedFontIndex != lastDrawnState.selectedFontIndex || // Mancava!
                newState.ntpDisabled != lastDrawnState.ntpDisabled ||             // Mancava!
                strcmp(newState.wifiStatus, lastDrawnState.wifiStatus) != 0 ||    // Mancava!
                newState.bluetoothEnabled != lastDrawnState.bluetoothEnabled ||   // Mancava BT!
                newState.bluetoothConnected != lastDrawnState.bluetoothConnected || // Mancava BT!
                strcmp(newState.bluetoothDeviceName, lastDrawnState.bluetoothDeviceName) != 0 || // Mancava BT!
                newState.pairedDevicesCount != lastDrawnState.pairedDevicesCount || // Mancava Dispositivi BT!
                newState.lastNtpSyncTime != lastDrawnState.lastNtpSyncTime ||
                alarmsChanged) {
                needsUpdate = true;
            }
        }
        // Aggiorna la schermata allarme se scatta il minuto durante la suoneria
        else if (newState.currentScreen == SCREEN_ALARM) {
            if (newState.minute != lastDrawnState.minute) {
                needsUpdate = true;
            }
        }

    if (!needsUpdate) {
        return; 
    }

    if (forceFullRefresh || exitingMenu) {
        Serial.println("[UI] Esecuzione Full Refresh...");
        display.setFullWindow();
    } else {
        Serial.println("[UI] Esecuzione Partial Refresh...");
        // Impone l'utilizzo esclusivo del partial refresh per tutta l'interfaccia utente.
        display.setPartialWindow(0, 0, display.width(), display.height());
    }

    display.firstPage();
    do {
        watchdog_feed();
        display.fillScreen(GxEPD_WHITE); // Sfondo bianco
        
        if (newState.currentScreen == SCREEN_HOME) {
            drawHomeScreen(newState);
        } else if (newState.currentScreen == SCREEN_MENU) {
            drawMenuScreen(newState);
        } else if (newState.currentScreen == SCREEN_ALARM) {
            drawAlarmScreen(newState);
        }

    } while (display.nextPage());
    

    lastDrawnState = newState;
}

void drawHomeScreen(const SystemState& state) {
    int displayWidth = display.width();
    int displayHeight = display.height();
    int leftWidth = (int)(displayWidth * 0.7);

    display.setTextColor(GxEPD_BLACK);

    // --- BARRA NOTIFICHE IN ALTO A SINISTRA (SOLO PER ERRORI/AVVISI) ---
    display.setFont(fonts[0]); // Usa sempre il font di default per la barra di stato
    display.setTextSize(1);

    int currentX = 10; // Inizia dal bordo sinistro

    // Mostra l'icona WiFi solo se c'è un errore e NTP non è disabilitato dall'utente
    if (!state.ntpDisabled && !state.ntpSynced) {
        drawWifiErrorIcon(currentX, 10, GxEPD_BLACK);
        currentX += 24; // Larghezza icona WiFi
        currentX += 10; // Padding
    }

    // Mostra l'icona e la percentuale della batteria solo se è scarica (es. < 20%)
    if (state.batteryPercent < 20) {
        drawBatteryIcon(currentX, 10 + 4, state.batteryPercent, GxEPD_BLACK);
        display.setCursor(currentX + 35, 10 + 18);
        display.printf("%d%%", state.batteryPercent);
        currentX += 70; // Larghezza stimata per icona + testo
        currentX += 10; // Padding (opzionale, se ci fossero altri elementi)
    }

    // --- ORARIO A SINISTRA ---
    // Usiamo il font custom gigante (solo numeri) a dimensione nativa
    display.setFont(&Futura_Medium_onlynumbers112pt7b);
    display.setTextSize(1);
    int timeX = 30;
    int timeY = 180; // Baseline adattata per font 112pt
    display.setCursor(timeX, timeY);
    
    display.printf("%02d:%02d", state.hour, state.minute);

    // --- PARTE DESTRA: COLONNA CON INFO AGGIUNTIVE ---
    int rightX = leftWidth + 85;
    int itemY = 46; // Centrato verticalmente: (272 - 180) / 2 = 46
    const int itemSpacing = 70;
    int textX = rightX + 50; // Aumentato per fare spazio alle icone da 40px

    // 1. PROSSIMA SVEGLIA (con font dedicato Futura 16pt)
    display.setFont(&Futura_Medium_alphanum_menu16pt7b);
    display.setTextSize(1);
    drawAlarmIcon(rightX, itemY, state.nextAlarmFound, GxEPD_BLACK);
    display.setCursor(textX, itemY + 34); // Abbassato ulteriormente per centrare la metà del testo con il centro dell'icona 40px
    if (state.nextAlarmFound) {
        display.printf("%02d:%02d", state.nextAlarmHour, state.nextAlarmMinute);
    } else {
        display.print("--:--");
    }

    // Se Bluetooth è attivo e connesso, sostituisci Temp/Umidità con l'icona gigante
    if (state.bluetoothEnabled && state.bluetoothConnected) {
        int btY = itemY + itemSpacing + 10; // Posizionato gradevolmente sotto l'allarme
        display.drawBitmap(rightX + 5, btY, icon_bluetooth_connected_96x96, 96, 96, GxEPD_WHITE, GxEPD_BLACK);
    } else {
        // Ripristina il font standard (selezionato dall'utente) per il resto delle informazioni
        display.setFont(fonts[state.selectedFontIndex % NUM_FONTS]);

        // 2. TEMPERATURA
        itemY += itemSpacing;
        drawThermometerIcon(rightX, itemY, GxEPD_BLACK);
        display.setCursor(textX, itemY + 26); // Baseline adattata per font standard
        display.printf("%.1f°C", state.temperature);

        // 3. UMIDITÀ
        itemY += itemSpacing;
        drawHumidityIcon(rightX, itemY, GxEPD_BLACK);
        display.setCursor(textX, itemY + 26); // Baseline adattata per font standard
        display.printf("%.0f%%", state.humidity);
    }
}

void drawMenuScreen(const SystemState& state) {
    int displayWidth = display.width();
    int tabWidth = displayWidth / NUM_TAB_ITEMS; // ~198 pixel con 792 di larghezza
    int tabHeight = 50; // Altezza della barra delle schede
    
    // --- 1. DISEGNO DELLA BARRA DELLE SCHEDE (HEADER) ---
    display.setFont(fonts[state.selectedFontIndex % NUM_FONTS]);
    display.setTextSize(1);

    const unsigned char* tabIcons[] = {
        icon_settings_40x40,
        icon_alarm_40x40,
        icon_audio_file_40x40,
        icon_settings_bluetooth_40x40
    };
    
    for (uint8_t i = 0; i < NUM_TAB_ITEMS; i++) {
        int startX = i * tabWidth;
        
        int16_t bx, by; uint16_t bw, bh;
        display.getTextBounds(tabItems[i], 0, 0, &bx, &by, &bw, &bh);
        int totalWidth = 40 + 5 + bw; // Larghezza icona (40) + spazio (5) + larghezza testo
        int iconX = startX + (tabWidth - totalWidth) / 2;
        int textX = iconX + 45; // 40px icona + 5px di margine
        int textY = 34; // Leggermente abbassato per allinearsi al centro dell'icona da 40px
        
        if (i == state.menuCursorIndex) {
            // Scheda Attiva: Sfondo Nero, Testo Bianco
            display.fillRect(startX, 0, tabWidth, tabHeight, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
            
            display.drawBitmap(iconX, 5, tabIcons[i], 40, 40, GxEPD_BLACK, GxEPD_WHITE);
            
            display.setCursor(textX, textY);
            display.print(tabItems[i]);
            
            // Disegna linee verticali di chiusura laterale (se non ai bordi estremi)
            if (i > 0) display.drawLine(startX, 0, startX, tabHeight, GxEPD_BLACK);
            if (i < NUM_TAB_ITEMS - 1) display.drawLine(startX + tabWidth, 0, startX + tabWidth, tabHeight, GxEPD_BLACK);
            
            // NON disegniamo la linea in basso per fare l'effetto scheda aperta
        } else {
            // Scheda Inattiva: Sfondo Bianco, Testo Nero
            display.setTextColor(GxEPD_BLACK);
            
            display.drawBitmap(iconX, 5, tabIcons[i], 40, 40, GxEPD_WHITE, GxEPD_BLACK);
            
            display.setCursor(textX, textY);
            display.print(tabItems[i]);
            
            // Disegna la linea in basso (che separa l'header dal contenuto)
            display.drawLine(startX, tabHeight, startX + tabWidth, tabHeight, GxEPD_BLACK);
            
            // Disegna il separatore verticale destro (tranne per l'ultimo elemento)
            if (i < NUM_TAB_ITEMS - 1) {
                // Se la prossima scheda è quella attiva, la riga verticale è già disegnata dal suo rettangolo
                if (i + 1 != state.menuCursorIndex) {
                    display.drawLine(startX + tabWidth, 10, startX + tabWidth, tabHeight - 10, GxEPD_BLACK);
                }
            }
        }
    }
    
    // Assicurati di rimettere il testo nero per il resto della UI
    display.setTextColor(GxEPD_BLACK);
    
    // --- 2. AREA CONTENUTI (Sotto l'Header) ---
    // Inizia a Y = tabHeight + 10 = 60
    
    int contentY = tabHeight + 20; // Y di partenza per i contenuti: 70
    
    switch (state.menuCursorIndex) {
        case 0:
            // --- 1. IMPOSTAZIONI ---
            // Layout a due colonne: opzioni modificabili a sinistra, info di sola lettura a destra.
            {
                // --- COLONNA SINISTRA: OPZIONI MODIFICABILI ---
                const int numEditableItems = 9;
                const char* fontNames[] = {"Default", "Futura 16pt", "Futura 8pt"};

                // --- Viewport / Finestra a Scorrimento ---
                int maxVisible = 8; // Numero massimo di voci che entrano nello schermo
                int startIdx = 0;
                
                if (state.menuItemIndex >= maxVisible) {
                    startIdx = state.menuItemIndex - maxVisible + 1;
                }

                int rightMargin = 520; // Margine destro per l'allineamento dei valori/toggle

                for (int i = 0; i < maxVisible; i++) {
                    int itemIdx = startIdx + i;
                    if (itemIdx >= numEditableItems) break;
                    
                    int yPos = contentY - 10 + (i * 25);
                    
                    if (itemIdx == state.menuItemIndex) {
                        display.fillRect(20, yPos - 2, 510, 24, GxEPD_BLACK);
                        display.setTextColor(GxEPD_WHITE);
                    } else {
                        display.setTextColor(GxEPD_BLACK);
                    }
                    
                    display.setCursor(30, yPos + 16);

                    if (itemIdx == 1 || itemIdx == 6) { // Gestione speciale per le barre grafiche (Volume e Luminosità)
                        display.print(itemIdx == 1 ? "Volume Sveglia" : "Luminosita' LED");
                        
                        uint16_t fgColor = (itemIdx == state.menuItemIndex) ? GxEPD_WHITE : GxEPD_BLACK;
                        uint16_t bgColor = (itemIdx == state.menuItemIndex) ? GxEPD_BLACK : GxEPD_WHITE;
                        
                        int rightIconX = rightMargin - 24;
                        int barWidth = 130;
                        int barX = rightIconX - 10 - barWidth;
                        int leftIconX = barX - 10 - 24;

                        const unsigned char* leftIcon = (itemIdx == 1) ? icon_volume_down_24x24 : icon_backlight_high_off_24x24;
                        const unsigned char* rightIcon = (itemIdx == 1) ? icon_volume_up_24x24 : icon_backlight_high_24x24;
                        uint8_t val = (itemIdx == 1) ? state.alarmVolume : state.ledBrightness;

                        display.drawBitmap(leftIconX, yPos - 2, leftIcon, 24, 24, bgColor, fgColor);
                        
                        int fillWidth = (val * barWidth) / 10;
                        display.drawRoundRect(barX, yPos + 2, barWidth, 16, 8, fgColor);
                        if (fillWidth > 0) {
                            display.fillRoundRect(barX, yPos + 2, fillWidth, 16, 8, fgColor);
                        }
                        
                        display.drawBitmap(rightIconX, yPos - 2, rightIcon, 24, 24, bgColor, fgColor);
                    } else {
                        char leftText[40] = "";
                        char rightText[30] = "";
                        bool hasToggle = false;
                        bool toggleOn = false;
                        const unsigned char* rightIcon = nullptr;

                        switch (itemIdx) {
                            case 0:
                                strcpy(leftText, "Abilita tutte le sveglie");
                                hasToggle = true;
                                toggleOn = state.alarmsGloballyEnabled;
                                break;
                            case 2:
                                strcpy(leftText, "Snooze");
                                sprintf(rightText, "%d min", state.snoozeTime);
                                break;
                            case 3:
                                strcpy(leftText, "Auto-Stop");
                                sprintf(rightText, "%d min", state.autoStopTime);
                                break;
                            case 4:
                                strcpy(leftText, "Sincronizzazione NTP automatica");
                                hasToggle = true;
                                toggleOn = !state.ntpDisabled;
                                break;
                            case 5:
                                strcpy(leftText, "Aggiorna NTP ora");
                                if (strncmp(state.wifiStatus, "↻", 3) == 0) {
                                    strcpy(rightText, "In corso...");
                                } else {
                                    rightIcon = icon_cached_24x24; // Usa la nuova icona
                                }
                                break;
                            case 6:
                                // Vuoto gestito nell'IF sopra
                                break;
                            case 7:
                                strcpy(leftText, "Auto-Spegnimento LED");
                                sprintf(rightText, "%ds", state.ledAutoOffTime);
                                break;
                            case 8:
                                strcpy(leftText, "Font UI");
                                strcpy(rightText, fontNames[state.selectedFontIndex % 3]);
                                break;
                        }

                        display.print(leftText);

                        // Disegna a destra o il toggle switch, l'icona o il valore della riga
                        if (hasToggle) {
                            drawToggleSwitch(rightMargin - 24, yPos - 2, toggleOn, itemIdx == state.menuItemIndex);
                        } else if (rightIcon != nullptr) {
                            uint16_t fgColor = (itemIdx == state.menuItemIndex) ? GxEPD_WHITE : GxEPD_BLACK;
                            uint16_t bgColor = (itemIdx == state.menuItemIndex) ? GxEPD_BLACK : GxEPD_WHITE;
                            display.drawBitmap(rightMargin - 24, yPos - 2, rightIcon, 24, 24, bgColor, fgColor);
                        } else if (strlen(rightText) > 0) {
                            int16_t x1, y1; uint16_t w, h;
                            display.getTextBounds(rightText, 0, 0, &x1, &y1, &w, &h);
                            display.setCursor(rightMargin - w, yPos + 16);
                            display.print(rightText);
                        }
                    }
                }
                display.setTextColor(GxEPD_BLACK);

                // --- COLONNA DESTRA: INFO DI SISTEMA (SOLA LETTURA) ---
                int rightColumnX = 550;
                display.drawLine(rightColumnX, tabHeight, rightColumnX, display.height(), GxEPD_BLACK);

                display.setFont(fonts[0]); // Usa un font standard e leggibile
                display.setTextSize(1);

                display.setCursor(rightColumnX + 20, contentY - 5);
                display.print("Info Sistema");
                display.drawLine(rightColumnX + 20, contentY, rightColumnX + 200, contentY, GxEPD_BLACK);

                int infoY = contentY + 30;

                // Info Batteria
                char batteryInfo[50];
                snprintf(batteryInfo, 50, "Batteria: %d%% (%.2fV)", state.batteryPercent, state.batteryVoltage);
                display.setCursor(rightColumnX + 20, infoY);
                display.print(batteryInfo);
                infoY += 25;

                // Lettura ADC in tempo reale per calibrazione
                uint32_t adc_reading = 0;
                for (int i = 0; i < 20; i++) adc_reading += analogRead(BAT_VOLT_PIN);
                adc_reading /= 20;
                char adcInfo[30];
                snprintf(adcInfo, 30, "ADC Grezzo: %d", adc_reading);
                display.setCursor(rightColumnX + 20, infoY);
                display.print(adcInfo);
                infoY += 35;

                // Info Sincronizzazione NTP
                display.setCursor(rightColumnX + 20, infoY);
                display.print("Ultima Sincro NTP:");
                
                display.setCursor(rightColumnX + 20, infoY + 20);
                display.print(state.wifiStatus);
                
                if (state.ntpSynced && state.lastNtpSyncTime > 0) {
                    char timeStr[30];
                    time_t lastSync = state.lastNtpSyncTime;
                    strftime(timeStr, sizeof(timeStr), "%d/%m/%Y %H:%M", localtime(&lastSync));
                    display.setCursor(rightColumnX + 20, infoY + 40);
                    display.print(timeStr);
                }
            }
            break;
            
        case 1:
            // --- 2. LISTA SVEGLIE (Ridisegnata come Lista Verticale) ---
            {
                int maxVisible = 4; // Mostra comodamente 4 righe
                int startIdx = 0;
                
                int totalItems = state.numConfiguredAlarms < 16 ? state.numConfiguredAlarms + 1 : 16;

                if (state.menuItemIndex >= maxVisible) {
                    startIdx = state.menuItemIndex - maxVisible + 1;
                }
                
                // Migliora lo scorrimento fluido all'ultima voce in basso
                if (state.menuItemIndex == totalItems - 1 && startIdx < totalItems - maxVisible) {
                    startIdx = totalItems - maxVisible;
                    if (startIdx < 0) startIdx = 0;
                }

                int boxX = 30;
                int boxWidth = displayWidth - 60;
                int rightMargin = boxX + boxWidth - 10;

                for (int i = 0; i < maxVisible; i++) {
                    int itemIdx = startIdx + i;
                    if (itemIdx >= totalItems) break;

                    int yPos = contentY + 5 + (i * 45); // Spaziatura verticale
                    
                    if (itemIdx == state.menuItemIndex) {
                        display.fillRect(boxX, yPos, boxWidth, 40, GxEPD_BLACK);
                        display.setTextColor(GxEPD_WHITE);
                    } else {
                        display.setTextColor(GxEPD_BLACK);
                        if (i > 0) display.drawLine(boxX, yPos - 2, boxX + boxWidth, yPos - 2, GxEPD_BLACK); // Riga di separazione
                    }

                    if (itemIdx == state.numConfiguredAlarms) {
                        // RIGA ADD ALARM (Solo icona centrale come richiesto)
                        uint16_t fgColor = (itemIdx == state.menuItemIndex) ? GxEPD_WHITE : GxEPD_BLACK;
                        uint16_t bgColor = (itemIdx == state.menuItemIndex) ? GxEPD_BLACK : GxEPD_WHITE;
                        
                        int centerX = boxX + (boxWidth / 2) - 12; // 12 è la metà di 24 (larghezza icona)
                        display.drawBitmap(centerX, yPos + 8, icon_alarm_add_24x24, 24, 24, bgColor, fgColor);
                    } else {
                        // RIGA SVEGLIA ESISTENTE
                        // 1. Numero Sveglia
                        display.setFont(fonts[0]);
                        display.setTextSize(1);
                        display.setCursor(boxX + 15, yPos + 26);
                        display.printf("#%d", itemIdx + 1);

                        // 2. Orario
                        display.setFont(fonts[state.selectedFontIndex % NUM_FONTS]);
                        int timeX = boxX + 80;
                        display.setCursor(timeX, yPos + 28);
                        
                        int hourStartX = display.getCursorX();
                        display.printf("%02d", state.alarms[itemIdx].hour);
                        int hourEndX = display.getCursorX();
                        
                        display.print(":");
                        
                        int minStartX = display.getCursorX();
                        display.printf("%02d", state.alarms[itemIdx].minute);
                        int minEndX = display.getCursorX();

                        // 3. Ricorrenza
                        display.setFont(fonts[0]);
                        int recX = boxX + 280; // Abbastanza spostato per non accavallarsi coi font grandi
                        display.setCursor(recX, yPos + 26);
                        
                        const char* recurrenceStr = "";
                        switch(state.alarms[itemIdx].recurrence) {
                            case ALARM_SINGLE:   recurrenceStr = "Singola"; break;
                            case ALARM_DAILY:    recurrenceStr = "Ogni Giorno"; break; 
                            case ALARM_WEEKDAYS: recurrenceStr = "Lun-Ven"; break;
                            case ALARM_WEEKENDS: recurrenceStr = "Sab-Dom"; break;
                        }
                        display.print(recurrenceStr);
                        
                        int16_t x1, y1; uint16_t w_rec, h_rec;
                        display.getTextBounds(recurrenceStr, 0, 0, &x1, &y1, &w_rec, &h_rec);

                        // 4. Icona Alarm ON / OFF
                        uint16_t fgColor = (itemIdx == state.menuItemIndex) ? GxEPD_WHITE : GxEPD_BLACK;
                        uint16_t bgColor = (itemIdx == state.menuItemIndex) ? GxEPD_BLACK : GxEPD_WHITE;
                        const unsigned char* alarmIcon = state.alarms[itemIdx].active ? icon_alarm_on_24x24 : icon_alarm_off_24x24;
                        display.drawBitmap(rightMargin - 24, yPos + 8, alarmIcon, 24, 24, bgColor, fgColor);

                        // 5. Cursori di Modifica (Modalità Edit)
                        if (itemIdx == state.menuItemIndex && state.isEditingAlarm) {
                            // Disegna l'icona Cestino/Elimina (X) a sinistra del toggle ON/OFF
                            display.drawBitmap(rightMargin - 55, yPos + 8, icon_delete_24x24, 24, 24, bgColor, fgColor);

                            int cursorY = yPos + 34; // Disegnato sotto al testo
                            uint16_t cursorColor = GxEPD_WHITE; // Visibile sempre perché lo sfondo è nero se evidenziato
                            
                            if (state.alarmEditFieldIndex == 0) {
                                display.fillRect(hourStartX, cursorY, hourEndX - hourStartX, 3, cursorColor);
                            } else if (state.alarmEditFieldIndex == 1) {
                                display.fillRect(minStartX, cursorY, minEndX - minStartX, 3, cursorColor);
                            } else if (state.alarmEditFieldIndex == 2) {
                                display.fillRect(recX + x1, cursorY, w_rec, 3, cursorColor);
                            } else if (state.alarmEditFieldIndex == 3) {
                                display.fillRect(rightMargin - 55, cursorY, 24, 3, cursorColor); // Cursore sotto l'icona X
                            }
                        }
                    }
                }
                display.setFont(NULL);
                display.setTextColor(GxEPD_BLACK);
            }
            break;
            
        case 2:
            // --- 3. TRACCE AUDIO ---
            {
                if (actualNumTracks == 0) {
                    display.setTextColor(GxEPD_BLACK);
                    display.setCursor(30, contentY + 26);
                    display.print("Nessun file MP3 trovato!");
                } else {
                    int maxVisible = 4; // Numero massimo di tracce visibili contemporaneamente nello schermo
                    int startIdx = 0;
                    
                    if (state.menuItemIndex < actualNumTracks) {
                        if (state.menuItemIndex >= maxVisible) {
                            startIdx = state.menuItemIndex - maxVisible + 1;
                        }
                    } else {
                        if (actualNumTracks > maxVisible) {
                            startIdx = actualNumTracks - maxVisible;
                        }
                    }

                    for (int i = 0; i < maxVisible; i++) {
                        int itemIdx = startIdx + i;
                        if (itemIdx >= actualNumTracks) break;

                        int yPos = contentY + 10 + (i * 40);
                        
                        if (itemIdx == state.menuItemIndex) {
                            // Espandi il riquadro di selezione fino a destra per accogliere l'onda
                            display.fillRect(20, yPos - 2, display.width() - 40, 32, GxEPD_BLACK);
                            display.setTextColor(GxEPD_WHITE);
                        } else {
                            display.setTextColor(GxEPD_BLACK);
                        }
                        
                        uint16_t itemColor = (itemIdx == state.menuItemIndex) ? GxEPD_WHITE : GxEPD_BLACK;
                        int circleX = 40;
                        int circleY = yPos + 14; // Centrato verticalmente rispetto al testo
                        
                        if (itemIdx == (state.currentTrackIndex % actualNumTracks)) {
                            display.fillCircle(circleX, circleY, 7, itemColor); // Radio button pieno
                        } else {
                            display.drawCircle(circleX, circleY, 7, itemColor); // Radio button vuoto
                            display.drawCircle(circleX, circleY, 6, itemColor); // Spessore aggiuntivo per renderlo più visibile
                        }
                        
                        display.setCursor(65, yPos + 22); // Sposta il testo a destra del pallino
                        
                        const char* displayName = trackNames[itemIdx];
                        if (displayName[0] == '/') displayName++; // Rimuove slash per display
                        display.print(displayName);

                        // --- DISEGNO ONDA AUDIO SIMMETRICA ALLINEATA A DESTRA ---
                        int waveX = display.width() - 170; // Allineata a 20px dal bordo destro (150 + 20)
                        int waveY = yPos + 14; // Asse di simmetria centrato verticalmente nel riquadro
                        
                        for (int b = 0; b < 50; b++) {
                            int barHeight = trackWaveforms[itemIdx][b];
                            if (barHeight < 1) barHeight = 1; 
                            if (barHeight > 14) barHeight = 14; // Limita altezza per non uscire dal box
                            
                            uint16_t barColor = (itemIdx == state.menuItemIndex) ? GxEPD_WHITE : GxEPD_BLACK;
                            
                            display.fillRect(waveX + (b * 3), waveY - barHeight, 2, barHeight * 2, barColor);
                        }

                        // Disegna dinamicamente l'icona Play/Stop sulla riga evidenziata
                        if (itemIdx == state.menuItemIndex) {
                            int iconX = waveX - 35; // Posizionata appena a sinistra dell'onda
                            int iconY = yPos + 6;   // Centrata verticalmente nel box (-2 + 8 = 6)
                            if (state.isPreviewPlaying) {
                                display.fillRect(iconX, iconY, 16, 16, GxEPD_WHITE);
                            } else {
                                display.fillTriangle(iconX, iconY, iconX, iconY + 16, iconX + 14, iconY + 8, GxEPD_WHITE);
                            }
                        }
                    }
                }
            }
            break;
            
        case 3:
            // --- 4. BLUETOOTH SPEAKER ---
            {
                int rightMargin = 520;
                int rightColumnX = 550;
                
                // --- COLONNA SINISTRA: OPZIONI ---
                for (int i = 0; i < 3; i++) {
                    int rowY = contentY + 10 + (i * 45);
                    
                    if (i == state.menuItemIndex) {
                        display.fillRect(20, rowY - 8, 510, 40, GxEPD_BLACK);
                        display.setTextColor(GxEPD_WHITE);
                    } else {
                        display.setTextColor(GxEPD_BLACK);
                    }
                    
                    uint16_t fgColor = (i == state.menuItemIndex) ? GxEPD_WHITE : GxEPD_BLACK;
                    uint16_t bgColor = (i == state.menuItemIndex) ? GxEPD_BLACK : GxEPD_WHITE;
                    
                    int textY = rowY + 18;
                    display.setCursor(65, textY);
                    
                    if (i == 0) {
                        const unsigned char* btIcon = state.bluetoothEnabled ? icon_media_bluetooth_on_24x24 : icon_media_bluetooth_off_24x24;
                        display.drawBitmap(30, rowY, btIcon, 24, 24, bgColor, fgColor);
                        display.print("Bluetooth");
                        drawToggleSwitch(rightMargin - 24, rowY, state.bluetoothEnabled, i == state.menuItemIndex);
                    } else if (i == 1) {
                        display.drawBitmap(30, rowY, icon_settings_bluetooth_24x24, 24, 24, bgColor, fgColor);
                        display.printf("Dispositivi associati (%d)", state.pairedDevicesCount);
                        display.drawBitmap(rightMargin - 24, rowY, icon_chevron_right_24x24, 24, 24, bgColor, fgColor);
                    } else if (i == 2) {
                        display.drawBitmap(30, rowY, icon_bluetooth_disabled_24x24, 24, 24, bgColor, fgColor);
                        display.print("Dimentica dispositivi");
                        
                        // Disegna un'icona (es. cestino/elimina) allineata a destra al posto del testo
                        display.drawBitmap(rightMargin - 24, rowY, icon_delete_24x24, 24, 24, bgColor, fgColor);
                    }
                }
                
                // --- COLONNA DESTRA: INFO STATO BLUETOOTH ---
                display.setTextColor(GxEPD_BLACK);
                display.drawLine(rightColumnX, tabHeight, rightColumnX, display.height(), GxEPD_BLACK);
                
                display.setFont(fonts[0]); // Font standard e leggibile
                display.setTextSize(1);
                
                display.setCursor(rightColumnX + 20, contentY - 5);
                display.print("Stato Bluetooth");
                display.drawLine(rightColumnX + 20, contentY, rightColumnX + 200, contentY, GxEPD_BLACK);
                
                int infoY = contentY + 30;
                
                if (state.bluetoothEnabled) {
                    if (state.bluetoothConnected) {
                        display.drawBitmap(rightColumnX + 20, infoY, icon_bluetooth_connected_24x24, 24, 24, GxEPD_WHITE, GxEPD_BLACK);
                        display.setCursor(rightColumnX + 50, infoY + 16);
                        display.print("Connesso");
                        
                        infoY += 40;
                        display.setCursor(rightColumnX + 20, infoY);
                        display.print("Dispositivo:");
                        display.setCursor(rightColumnX + 20, infoY + 20);
                        display.print(state.bluetoothDeviceName);
                    } else {
                        display.drawBitmap(rightColumnX + 20, infoY, icon_bluetooth_24x24, 24, 24, GxEPD_WHITE, GxEPD_BLACK);
                        display.setCursor(rightColumnX + 50, infoY + 16);
                        display.print("In attesa...");
                        
                        infoY += 40;
                        display.setCursor(rightColumnX + 20, infoY);
                        display.print("Nome visibile:");
                        display.setCursor(rightColumnX + 20, infoY + 20);
                        display.print("Sveglia Smart");
                    }
                    
                    infoY += 45;
                    display.setCursor(rightColumnX + 20, infoY);
                    display.print("Auto-spegnimento:");
                    display.setCursor(rightColumnX + 20, infoY + 20);
                    display.print("se inattivo 5 min.");
                } else {
                    display.drawBitmap(rightColumnX + 20, infoY, icon_bluetooth_disabled_24x24, 24, 24, GxEPD_WHITE, GxEPD_BLACK);
                    display.setCursor(rightColumnX + 50, infoY + 16);
                    display.print("Disattivato");
                }
                
                display.setFont(fonts[state.selectedFontIndex % NUM_FONTS]);
            }
            break;
    }
    
    // --- 3. MESSAGGIO ERRORE AUDIO (se presente) ---
    if (state.audioError) {
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(20, 230);
        display.print("Errore: File audio non trovato!");
    }
    
    // --- 4. ISTRUZIONI (In basso) ---
    // Mostra le istruzioni solo quando si modifica una sveglia (controlli non standard)
    if (state.currentScreen == SCREEN_MENU && state.menuCursorIndex == 1 && state.isEditingAlarm) {
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(1);
        display.setCursor(20, 260);
        if (state.alarmEditFieldIndex == 3) {
            display.print("[ A: Cambia Campo ]  [ B: ELIMINA SVEGLIA ]  [ A Lungo: Salva ]");
        } else {
            display.print("[ A: Cambia Campo ]  [ A Lungo: Salva ]  [ B: Avanti ]  [ B Lungo: Indietro ]");
        }
    }
}

void drawAlarmScreen(const SystemState& state) {
    display.setTextColor(GxEPD_BLACK);
    
    // --- 1. ETICHETTE TASTI (IN ALTO) ---
    display.setFont(&Futura_Medium_alphanum_menu16pt7b);
    display.setTextSize(1);
    
    int16_t x1, y1;
    uint16_t w, h;
    
    // Posizioni centrali ideali per i due tasti fisici
    int leftCenterX = 150;
    int rightCenterX = display.width() - 150;
    
    // Tasto A (Sinistra) - SNOOZE
    const char* snoozeText = "SNOOZE";
    display.getTextBounds(snoozeText, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(leftCenterX - (w / 2), 28); // Più in alto e perfettamente centrato
    display.print(snoozeText);
    
    char snoozeSub[20];
    snprintf(snoozeSub, sizeof(snoozeSub), "+%d min", state.snoozeTime);
    display.getTextBounds(snoozeSub, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(leftCenterX - (w / 2), 58);
    display.print(snoozeSub);
    
    // Tasto B (Destra) - STOP
    const char* stopText = "STOP";
    display.getTextBounds(stopText, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(rightCenterX - (w / 2), 28); // Più in alto e perfettamente centrato
    display.print(stopText);

    // --- 2. ORARIO GIGANTE AL CENTRO CON DUE PUNTI LAMPEGGIANTI ---
    display.setFont(&Futura_Medium_onlynumbers112pt7b);
    display.setTextSize(1); // Nessun moltiplicatore: usiamo la grandezza nativa a 112pt
    
    // Calcolo per centrare matematicamente il testo (su 792x272)
    // NOTA: I font custom usano il parametro Y come "baseline" (la linea di base delle lettere),
    // non l'angolo in alto a sinistra. Un font 144pt necessita di una Y molto più profonda.
    // Potresti dover variare leggermente questi due valori per centrarlo perfettamente nel tuo display.
    int timeX = 120; 
    int timeY = 205;
    
    display.setCursor(timeX, timeY);
    
    // Stampa fissa dell'orario senza animazione
    display.printf("%02d:%02d", state.hour, state.minute);
}