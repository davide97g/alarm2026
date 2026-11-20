#include "logic.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

// NOTA: le espressioni in virgola mobile sono copiate tali e quali dal codice
// originale (stessi tipi, stesse funzioni): i test di non regressione
// confrontano questi risultati con la versione precedente bit per bit.

// --- Batteria ---------------------------------------------------------------

int32_t arduinoMap(int32_t x, int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax) {
    const int32_t run = inMax - inMin;
    if (run == 0) return -1;
    return ((x - inMin) * (outMax - outMin)) / run + outMin;
}

int32_t batteryMvFromAdc(uint32_t adc, uint16_t adcMin, uint16_t adcMax) {
    // Calibrazione da multimetro: 4.21 V (carica) .. 2.80 V (cut-off)
    return arduinoMap((int32_t)adc, adcMin, adcMax, 2800, 4210);
}

float batteryVoltageFromMv(int32_t mv) {
    float voltage = mv / 1000.0f;
    if (voltage < 2.5f) voltage = 2.5f;
    if (voltage > 4.3f) voltage = 4.3f;
    return voltage;
}

int batteryPercentFromMv(int32_t mv) {
    // Punti della curva di scarica Li-Ion, interpolati linearmente tra loro
    static const int16_t MV[]  = {4200, 4050, 3950, 3850, 3750, 3650, 3550, 3450, 3350, 3150, 2950, 2800};
    static const int8_t  PCT[] = { 100,   90,   80,   70,   60,   50,   40,   30,   20,   10,    5,    0};
    if (mv >= MV[0]) return 100;
    for (int i = 1; i < (int)(sizeof(MV) / sizeof(MV[0])); i++) {
        if (mv >= MV[i]) return arduinoMap(mv, MV[i], MV[i - 1], PCT[i], PCT[i - 1]);
    }
    return 0;
}

int batteryHysteresis(int percent, int lastPercent) {
    if (lastPercent != -1 && abs(percent - lastPercent) < 2) return lastPercent;
    return percent;
}

// --- Sveglie ----------------------------------------------------------------

bool alarmDayMatches(AlarmRecurrence recurrence, int dayOfWeek) {
    switch (recurrence) {
        case ALARM_SINGLE:
        case ALARM_DAILY:    return true;
        case ALARM_WEEKDAYS: return dayOfWeek >= 1 && dayOfWeek <= 5;
        case ALARM_WEEKENDS: return dayOfWeek == 0 || dayOfWeek == 6;
    }
    return false;
}

bool isInRingWindow(int currentMins, int targetMins) {
    int diff = abs(currentMins - targetMins);
    return diff <= 2 || diff >= 1438;
}

NextAlarm findNextAlarm(const Alarm* alarms, uint8_t count, int dayOfWeek, int currentMins) {
    NextAlarm best = {false, 0, 0, 0, 0xFFFFFFFF};
    for (uint8_t i = 0; i < count; i++) {
        const Alarm& al = alarms[i];
        if (!al.active) continue;
        int alarmMins = al.hour * 60 + al.minute;
        // Primo giorno utile: oggi solo se l'orario non è già passato
        for (int dayOffset = 0; dayOffset < 30; dayOffset++) {
            if (!alarmDayMatches(al.recurrence, (dayOfWeek + dayOffset) % 7)) continue;
            if (dayOffset == 0 && alarmMins <= currentMins) continue;
            uint32_t futureMinutes = (dayOffset * 24 * 60) + alarmMins - currentMins;
            if (futureMinutes < best.minutesAway) {
                best = {true, al.hour, al.minute, dayOffset, futureMinutes};
            }
            break;
        }
    }
    return best;
}

void snoozeTarget(int hour, int minute, uint8_t snoozeTime, uint8_t* outHour, uint8_t* outMinute) {
    int snoozeMins = hour * 60 + minute + snoozeTime;
    *outHour = (snoozeMins / 60) % 24;
    *outMinute = snoozeMins % 60;
}

// --- LED e audio ------------------------------------------------------------

uint32_t ledCurveDuty(uint8_t brightness) {
    float t = (brightness - 1.0f) / 9.0f;
    float curve = std::pow(t, 2.0f);
    return std::round(5.0f + (250.0f * curve)); // Minimo 5, massimo 255
}

uint8_t alarmFadeDuty(uint8_t maxDuty, int64_t elapsedUs) {
    const int64_t halfCycleTimeUs = 6000000LL; // 6 s salita, 6 s discesa
    int64_t timeInCycleUs = elapsedUs % (halfCycleTimeUs * 2);
    float fadeIndex;
    if (timeInCycleUs < halfCycleTimeUs) {
        fadeIndex = (float)timeInCycleUs / (float)halfCycleTimeUs;
    } else {
        fadeIndex = 1.0f - ((float)(timeInCycleUs - halfCycleTimeUs) / (float)halfCycleTimeUs);
    }
    return std::round((float)maxDuty * fadeIndex);
}

float volumeGain(uint8_t volume) {
    if (volume < 1) volume = 1;
    if (volume > 10) volume = 10;
    float t = (volume - 1.0f) / 9.0f;
    float curve = std::pow(t, 3.0f);
    return 0.03f + (curve * 0.8f);
}

// --- Menù e UI --------------------------------------------------------------

uint8_t menuItemCount(const SystemState& state, uint8_t numTracks) {
    switch (state.menuCursorIndex) {
        case 0: return 9; // Impostazioni
        case 1: return state.numConfiguredAlarms < 16 ? state.numConfiguredAlarms + 1 : 16; // Sveglie + "aggiungi"
        case 2: return numTracks; // Tracce
        case 3: return 3; // Bluetooth
    }
    return 0;
}

static bool alarmsDiffer(const SystemState& a, const SystemState& b) {
    for (int i = 0; i < 16; i++) {
        if (a.alarms[i].hour != b.alarms[i].hour || a.alarms[i].minute != b.alarms[i].minute ||
            a.alarms[i].active != b.alarms[i].active || a.alarms[i].recurrence != b.alarms[i].recurrence) {
            return true;
        }
    }
    return false;
}

UiDecision uiDecide(const SystemState& n, const SystemState& l, bool forceFullRefresh) {
    UiDecision d = {forceFullRefresh, false, false};
    if (n.currentScreen != l.currentScreen) {
        d.needsUpdate = true;
        d.screenChanged = true;
        d.exitingMenu = l.currentScreen == SCREEN_MENU && n.currentScreen == SCREEN_HOME;
    }

    if (n.currentScreen == SCREEN_HOME) {
        if (n.minute != l.minute || std::abs(n.temperature - l.temperature) >= 0.5 ||
            n.batteryPercent != l.batteryPercent || n.day != l.day ||
            n.nextAlarmFound != l.nextAlarmFound || n.nextAlarmHour != l.nextAlarmHour ||
            n.nextAlarmMinute != l.nextAlarmMinute || n.ntpSynced != l.ntpSynced) {
            d.needsUpdate = true;
        }
    } else if (n.currentScreen == SCREEN_MENU) {
        if (n.menuCursorIndex != l.menuCursorIndex || n.menuItemIndex != l.menuItemIndex ||
            n.isEditingAlarm != l.isEditingAlarm || n.alarmEditFieldIndex != l.alarmEditFieldIndex ||
            n.numConfiguredAlarms != l.numConfiguredAlarms || n.alarmVolume != l.alarmVolume ||
            n.snoozeTime != l.snoozeTime || n.autoStopTime != l.autoStopTime ||
            n.alarmsGloballyEnabled != l.alarmsGloballyEnabled || n.ledBrightness != l.ledBrightness ||
            n.ledAutoOffTime != l.ledAutoOffTime || n.audioError != l.audioError ||
            n.isPreviewPlaying != l.isPreviewPlaying ||
            std::abs(n.batteryVoltage - l.batteryVoltage) > 0.01 ||
            n.currentTrackIndex != l.currentTrackIndex || n.selectedFontIndex != l.selectedFontIndex ||
            n.ntpDisabled != l.ntpDisabled || strcmp(n.wifiStatus, l.wifiStatus) != 0 ||
            n.bluetoothEnabled != l.bluetoothEnabled || n.bluetoothConnected != l.bluetoothConnected ||
            strcmp(n.bluetoothDeviceName, l.bluetoothDeviceName) != 0 ||
            n.pairedDevicesCount != l.pairedDevicesCount || n.lastNtpSyncTime != l.lastNtpSyncTime ||
            alarmsDiffer(n, l)) {
            d.needsUpdate = true;
        }
    } else if (n.currentScreen == SCREEN_ALARM) {
        if (n.minute != l.minute) d.needsUpdate = true;
    }
    return d;
}

// --- Main loop --------------------------------------------------------------

uint32_t loopSleepMs(int64_t nowUs, int64_t lastActivityUs, const SystemState& state, bool ledOn) {
    bool home = state.currentScreen == SCREEN_HOME;
    int64_t next = lastActivityUs + INACTIVITY_TIMEOUT_US;
    if (home && ledOn) {
        next = lastActivityUs + ((int64_t)state.ledAutoOffTime * 1000000LL);
    }
    if (state.bluetoothEnabled && home && !state.bluetoothConnected) {
        int64_t btTimeout = lastActivityUs + BT_IDLE_TIMEOUT_US;
        if (btTimeout < next) next = btTimeout;
    }
    uint32_t sleepMs = next > nowUs ? (uint32_t)((next - nowUs) / 1000) : 0;
    // Mai oltre 5 s, così il watchdog (15 s) viene sempre nutrito
    return sleepMs > 5000 ? 5000 : sleepMs;
}

// --- Pulsanti ---------------------------------------------------------------

static const int64_t DEBOUNCE_US = 30000;

InputEvent ButtonFsm::step(bool rawA, bool rawB, int64_t now) {
    if (rawA && !stateA) { stateA = true; pressTimeA = now; handledA = inCombo; }
    if (rawB && !stateB) { stateB = true; pressTimeB = now; handledB = inCombo; }

    // Rilascio entro il debounce: era un rimbalzo, ignoralo
    if (!rawA) {
        if (stateA && !handledA && (now - pressTimeA < DEBOUNCE_US)) handledA = true;
        stateA = false;
    }
    if (!rawB) {
        if (stateB && !handledB && (now - pressTimeB < DEBOUNCE_US)) handledB = true;
        stateB = false;
    }

    if (stateA && stateB) {
        if (!inCombo) {
            inCombo = true;
            comboStart = now;
            comboHandled = false;
            handledA = true;
            handledB = true;
        } else if (!comboHandled && (now - comboStart >= longPressUs)) {
            comboHandled = true;
            return INPUT_BTN_AB_LONG;
        }
    } else if (inCombo && !rawA && !rawB) {
        inCombo = false;
        if (!comboHandled && (now - comboStart >= DEBOUNCE_US)) {
            comboHandled = true;
            return INPUT_BTN_AB_SHORT;
        }
    }

    if (inCombo || (rawA && handledA) || (rawB && handledB)) return INPUT_NONE;

    if (stateA) {
        if (!handledA && (now - pressTimeA >= longPressUs)) { handledA = true; return INPUT_BTN_A_LONG; }
    } else if (!handledA && pressTimeA > 0 && (now - pressTimeA >= DEBOUNCE_US)) {
        handledA = true; pressTimeA = 0; return INPUT_BTN_A_SHORT;
    }

    if (stateB) {
        if (!handledB && (now - pressTimeB >= longPressUs)) { handledB = true; return INPUT_BTN_B_LONG; }
    } else if (!handledB && pressTimeB > 0 && (now - pressTimeB >= DEBOUNCE_US)) {
        handledB = true; pressTimeB = 0; return INPUT_BTN_B_SHORT;
    }

    if (!stateA && handledA) pressTimeA = 0;
    if (!stateB && handledB) pressTimeB = 0;
    return INPUT_NONE;
}
