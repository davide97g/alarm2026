#ifndef STATEMANAGER_H
#define STATEMANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "SystemState.h"

class StateManager {
private:
    Preferences prefs;

public:
    void begin();
    
    // Inizializza le variabili transitorie nominali e carica le preferenze dalla NVS
    void loadAll(SystemState& state);
    
    // Funzioni helper per salvare e aggiornare il modello
    void setAlarmVolume(SystemState& state, uint8_t vol);
    void setSnoozeTime(SystemState& state, uint8_t snooze);
    void setAutoStopTime(SystemState& state, uint8_t autoStop);
    void setAlarmsGloballyEnabled(SystemState& state, bool enabled);
    void setLedBrightness(SystemState& state, uint8_t brightness);
    void setLedAutoOffTime(SystemState& state, uint8_t time);
    void setFontIndex(SystemState& state, uint8_t index);
    void setTrackIndex(SystemState& state, uint8_t index);
    void setNtpDisabled(SystemState& state, bool disabled);
    void setNtpSynced(SystemState& state, bool synced, time_t syncTime);
    void setBluetoothEnabled(SystemState& state, bool enabled);
    
    // Gestione sveglie (migrata da main.cpp)
    void saveAllAlarms(SystemState& state);
    void deleteAlarm(SystemState& state, uint8_t index);
};

// Istanza globale esportata
extern StateManager stateManager;

#endif // STATEMANAGER_H
