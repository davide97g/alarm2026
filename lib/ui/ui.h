#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include "../../include/config.h" // Inclusione delle costanti centralizzate


enum ScreenType {
    SCREEN_HOME = 0,
    SCREEN_MENU,
    SCREEN_ALARM
};

// Tipi di ricorrenza per la singola sveglia
enum AlarmRecurrence {
    ALARM_SINGLE = 0,   // Suona una volta sola e si disattiva
    ALARM_DAILY,        // Tutti i giorni
    ALARM_WEEKDAYS,     // Feriali (Lunedì - Venerdì)
    ALARM_WEEKENDS      // Weekend (Sabato - Domenica)
};

// Struttura che rappresenta una singola sveglia
struct Alarm {
    uint8_t hour;
    uint8_t minute;
    bool active;
    AlarmRecurrence recurrence;
};

// Struttura che rappresenta lo stato globale del sistema (il Modello)
// Contiene tutti i dati necessari per aggiornare l'interfaccia utente.
struct SystemState {
    uint8_t hour;
    uint8_t minute;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t dayOfWeek; // 0=Domenica, 1=Lunedì...

    float temperature;
    float humidity;
    float batteryVoltage;       // Voltaggio attuale batteria
    uint8_t batteryPercent;
    uint16_t batAdcMin;         // Min ADC per batteria (configurabile)
    uint16_t batAdcMax;         // Max ADC per batteria (configurabile)
    unsigned long longPressMs;  // Tempo per long press (configurabile)
    
    // --- Nuove variabili per la navigazione ---
    ScreenType currentScreen; // Schermata attualmente attiva
    uint8_t menuCursorIndex;  // Indice della voce di menù (scheda) attualmente selezionata
    uint8_t menuItemIndex;    // Indice dell'opzione (riga) attualmente evidenziata
    uint8_t currentTrackIndex;// Indice della traccia audio attualmente impostata per la sveglia
    bool isEditingAlarm;      // Flag per indicare se siamo in modalità modifica sveglia
    uint8_t alarmEditFieldIndex; // 0=Ora, 1=Minuti, 2=Ricorrenza
    bool nextAlarmFound;      // Flag che indica se c'è una prossima sveglia attiva
    uint8_t nextAlarmHour;    // Ora della prossima sveglia
    uint8_t nextAlarmMinute;  // Minuto della prossima sveglia

    // --- Variabili per le impostazioni ---
    uint8_t alarmVolume;        // Volume sveglia (1-10)
    uint8_t snoozeTime;         // Minuti di snooze (1-20)
    uint8_t autoStopTime;       // Minuti di auto-spegnimento sveglia (1-5)
    bool alarmsGloballyEnabled; // Master switch per tutte le sveglie
    Alarm alarms[16];           // Array per contenere i 16 slot sveglia
    uint8_t numConfiguredAlarms;// Numero di sveglie create dall'utente (0-16)
    uint8_t ledBrightness;      // Luminosità LED (0-10)
    uint8_t ledAutoOffTime;     // Tempo auto spegnimento LED (5-30s)
    uint8_t selectedFontIndex;   // Indice del font selezionato (0=FreeSans9pt, 1=FreeSansBold24pt, 2+=personalizzati)
    bool isPreviewPlaying;      // Flag che indica se è in corso un'anteprima audio
    bool audioError;            // Flag per errore riproduzione audio (temporaneo)
    
    // --- Variabili per NTP (Network Time Protocol) ---
    bool ntpSynced;             // Vero se orario è stato sincronizzato almeno una volta via NTP
    bool ntpDisabled;           // Vero se utente ha disabilitato la sincronizzazione NTP
    char wifiStatus[40];        // Stato WiFi: "✓ OK", "✗ Rete non trovata", ecc. (max 40 char)
    time_t lastNtpSyncTime;     // Timestamp (unixtime) dell'ultima sincronizzazione riuscita

    bool bluetoothEnabled;
    bool bluetoothConnected;
    char bluetoothDeviceName[30];
    uint8_t pairedDevicesCount;
};

// Funzione di inizializzazione della logica UI
void ui_init();

// Funzione principale del front-end.
// Riceve in input lo stato aggiornato dai sensori e decide:
// 1. Se è necessario aggiornare lo schermo
// 2. Se eseguire un aggiornamento parziale o totale
// forceFullRefresh forzerà l'aggiornamento totale se impostato a 'true'
void ui_update(const SystemState& newState, bool forceFullRefresh = false);

#endif // UI_H