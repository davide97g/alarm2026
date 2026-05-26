#include "StateManager.h"
#include "../../include/config.h"

StateManager stateManager;

void StateManager::begin() {
    prefs.begin("sveglia", false);
}

void StateManager::loadAll(SystemState& state) {
    // 1. Inizializzazione sicura nominale (i Default)
    state.hour = 12;
    state.minute = 30;
    state.day = 1;
    state.month = 1;
    state.year = 2000;
    state.dayOfWeek = 0;
    state.temperature = 22.5f;
    state.humidity = 45.0f;
    state.batteryVoltage = 4.2f;
    state.batteryPercent = 100;
    
    state.currentScreen = SCREEN_HOME;
    state.menuCursorIndex = 0;
    state.menuItemIndex = 0;
    state.isEditingAlarm = false;
    state.alarmEditFieldIndex = 0;
    state.nextAlarmFound = false;
    state.nextAlarmHour = 255;
    state.nextAlarmMinute = 255;
    
    state.isPreviewPlaying = false;
    state.audioError = false;
    strncpy(state.wifiStatus, "✗ Non sincron.", sizeof(state.wifiStatus));
    state.bluetoothConnected = false;
    strncpy(state.bluetoothDeviceName, "Nessuno", sizeof(state.bluetoothDeviceName));
    state.pairedDevicesCount = 0;

    state.isSnoozeActive = false;
    state.snoozeTargetHour = 0;
    state.snoozeTargetMinute = 0;
    state.lastSyncDay = -1;
    state.lastNtpAttempt = 0;
    state.lastBatteryHour = 255;

    // 2. Caricamento dalla NVS (Preferences)
    state.batAdcMin = prefs.getUShort("bat_min", BAT_ADC_MIN_DEFAULT);
    state.batAdcMax = prefs.getUShort("bat_max", BAT_ADC_MAX_DEFAULT);
    state.longPressMs = prefs.getULong("long_press_ms", LONG_PRESS_MS_DEFAULT);
    
    state.alarmVolume = prefs.getUChar("vol", 7);
    state.snoozeTime = prefs.getUChar("snooze", 5);
    state.autoStopTime = prefs.getUChar("autostop", 3);
    state.alarmsGloballyEnabled = prefs.getBool("al_global", true);
    
    state.ledBrightness = prefs.getUChar("led_bri", 5);
    state.ledAutoOffTime = prefs.getUChar("led_time", 10);
    state.selectedFontIndex = prefs.getUChar("font_idx", 0);
    state.currentTrackIndex = prefs.getUChar("track_idx", 0);
    
    state.ntpDisabled = prefs.getBool("ntp_disabled", false);
    state.ntpSynced = prefs.getBool("ntp_synced", false);
    state.lastNtpSyncTime = prefs.getULong("last_ntp", 0);
    state.bluetoothEnabled = prefs.getBool("bt_enabled", false);

    // Caricamento Sveglie
    state.numConfiguredAlarms = prefs.getUChar("num_alarms", 0);
    for (uint8_t i = 0; i < 16; i++) {
        char key[15];
        snprintf(key, sizeof(key), "a%d_h", i);
        state.alarms[i].hour = prefs.getUChar(key, 7);
        snprintf(key, sizeof(key), "a%d_m", i);
        state.alarms[i].minute = prefs.getUChar(key, 0);
        snprintf(key, sizeof(key), "a%d_act", i);
        state.alarms[i].active = prefs.getBool(key, false);
        snprintf(key, sizeof(key), "a%d_rec", i);
        state.alarms[i].recurrence = (AlarmRecurrence)prefs.getUChar(key, ALARM_SINGLE);
    }
}

void StateManager::setAlarmVolume(SystemState& state, uint8_t vol) {
    state.alarmVolume = vol;
    prefs.putUChar("vol", vol);
}

void StateManager::setSnoozeTime(SystemState& state, uint8_t snooze) {
    state.snoozeTime = snooze;
    prefs.putUChar("snooze", snooze);
}

void StateManager::setAutoStopTime(SystemState& state, uint8_t autoStop) {
    state.autoStopTime = autoStop;
    prefs.putUChar("autostop", autoStop);
}

void StateManager::setAlarmsGloballyEnabled(SystemState& state, bool enabled) {
    state.alarmsGloballyEnabled = enabled;
    prefs.putBool("al_global", enabled);
}

void StateManager::setLedBrightness(SystemState& state, uint8_t brightness) {
    state.ledBrightness = brightness;
    prefs.putUChar("led_bri", brightness);
}

void StateManager::setLedAutoOffTime(SystemState& state, uint8_t time) {
    state.ledAutoOffTime = time;
    prefs.putUChar("led_time", time);
}

void StateManager::setFontIndex(SystemState& state, uint8_t index) {
    state.selectedFontIndex = index;
    prefs.putUChar("font_idx", index);
}

void StateManager::setTrackIndex(SystemState& state, uint8_t index) {
    state.currentTrackIndex = index;
    prefs.putUChar("track_idx", index);
}

void StateManager::setNtpDisabled(SystemState& state, bool disabled) {
    state.ntpDisabled = disabled;
    prefs.putBool("ntp_disabled", disabled);
}

void StateManager::setNtpSynced(SystemState& state, bool synced, time_t syncTime) {
    state.ntpSynced = synced;
    state.lastNtpSyncTime = syncTime;
    prefs.putBool("ntp_synced", synced);
    prefs.putULong("last_ntp", syncTime);
}

void StateManager::setBluetoothEnabled(SystemState& state, bool enabled) {
    state.bluetoothEnabled = enabled;
    prefs.putBool("bt_enabled", enabled);
}

void StateManager::saveAllAlarms(SystemState& state) {
    prefs.putUChar("num_alarms", state.numConfiguredAlarms);
    for (uint8_t i = 0; i < 16; i++) {
        char key[15];
        snprintf(key, sizeof(key), "a%d_h", i);
        prefs.putUChar(key, state.alarms[i].hour);
        snprintf(key, sizeof(key), "a%d_m", i);
        prefs.putUChar(key, state.alarms[i].minute);
        snprintf(key, sizeof(key), "a%d_act", i);
        prefs.putBool(key, state.alarms[i].active);
        snprintf(key, sizeof(key), "a%d_rec", i);
        prefs.putUChar(key, (uint8_t)state.alarms[i].recurrence);
    }
}

void StateManager::deleteAlarm(SystemState& state, uint8_t index) {
    if (index >= state.numConfiguredAlarms) return;
    for (uint8_t i = index; i < state.numConfiguredAlarms - 1; i++) {
        state.alarms[i] = state.alarms[i + 1];
    }
    state.numConfiguredAlarms--;
    saveAllAlarms(state);
}
