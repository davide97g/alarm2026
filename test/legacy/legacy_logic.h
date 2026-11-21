// Copie CONGELATE della logica originale (commit 769dae5), prese da main.cpp,
// alarm.cpp, input.cpp, led.cpp, audio.cpp e ui.cpp. Le espressioni sono
// identiche all'originale; cambia solo l'I/O (parametri al posto di hardware e
// variabili globali). Riferimento per i test differenziali: non modificarle.
#ifndef LEGACY_LOGIC_H
#define LEGACY_LOGIC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include "../../lib/state/SystemState.h"
#include "../../lib/input/input_events.h"

namespace legacy {

using std::abs;
using std::pow;
using std::round;

// map() di Arduino ESP32 (WMath.cpp). Su ESP32 long è a 32 bit: int32_t qui.
inline int32_t map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max) {
    const int32_t run = in_max - in_min;
    if (run == 0) return -1;
    const int32_t rise = out_max - out_min;
    const int32_t delta = x - in_min;
    return (delta * rise) / run + out_min;
}

// main.cpp: updateBatteryOnly() dopo la media ADC
struct Battery { float voltage; int percent; };
inline Battery updateBatteryOnly(uint32_t adc_reading, uint16_t batAdcMin, uint16_t batAdcMax, int& lastBatteryPercent) {
    int32_t mapped_mv = map(adc_reading, batAdcMin, batAdcMax, 2800, 4210);
    float voltage = mapped_mv / 1000.0f;
    if (voltage < 2.5f) voltage = 2.5f;
    if (voltage > 4.3f) voltage = 4.3f;
    int percent;
    if (mapped_mv >= 4200)      percent = 100;
    else if (mapped_mv >= 4050) percent = map(mapped_mv, 4050, 4200, 90, 100);
    else if (mapped_mv >= 3950) percent = map(mapped_mv, 3950, 4050, 80, 90);
    else if (mapped_mv >= 3850) percent = map(mapped_mv, 3850, 3950, 70, 80);
    else if (mapped_mv >= 3750) percent = map(mapped_mv, 3750, 3850, 60, 70);
    else if (mapped_mv >= 3650) percent = map(mapped_mv, 3650, 3750, 50, 60);
    else if (mapped_mv >= 3550) percent = map(mapped_mv, 3550, 3650, 40, 50);
    else if (mapped_mv >= 3450) percent = map(mapped_mv, 3450, 3550, 30, 40);
    else if (mapped_mv >= 3350) percent = map(mapped_mv, 3350, 3450, 20, 30);
    else if (mapped_mv >= 3150) percent = map(mapped_mv, 3150, 3350, 10, 20);
    else if (mapped_mv >= 2950) percent = map(mapped_mv, 2950, 3150, 5, 10);
    else if (mapped_mv >= 2800) percent = map(mapped_mv, 2800, 2950, 0, 5);
    else                        percent = 0;
    if (lastBatteryPercent != -1) {
        if (abs(percent - lastBatteryPercent) < 2) {
            percent = lastBatteryPercent;
        }
    }
    lastBatteryPercent = percent;
    return {voltage, percent};
}

// alarm.cpp: calculateNextAlarm(), ricerca (senza RTC)
struct Next { bool found; uint8_t hour, minute; int dayOffset; uint32_t minutes; };
inline Next calculateNextAlarm(const SystemState& appState, int currentDayOfWeek, int currentMinsFromMidnight) {
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
            if (dayMatches) break;
        }
    }
    return {found, nextHour, nextMinute, bestDayOffset, minFutureMinutes};
}

// main.cpp: performWakeupTasks(), verifica sveglie (true = suona; disattiva le singole)
inline bool shouldRing(SystemState& appState, int currentDayOfWeek, int currentMins, int* savesOut) {
    bool shouldRing = false;
    *savesOut = 0;
    if (appState.isSnoozeActive) {
        int snoozeMinsFromMidnight = appState.snoozeTargetHour * 60 + appState.snoozeTargetMinute;
        int diff = abs(currentMins - snoozeMinsFromMidnight);
        if (diff <= 2 || diff >= 1438) {
            shouldRing = true;
            appState.isSnoozeActive = false;
        }
    }
    if (!shouldRing) {
        for (uint8_t i = 0; i < appState.numConfiguredAlarms; i++) {
            int alarmMins = appState.alarms[i].hour * 60 + appState.alarms[i].minute;
            int diff = abs(currentMins - alarmMins);
            if (appState.alarms[i].active && (diff <= 2 || diff >= 1438)) {
                bool dayMatches = false;
                switch (appState.alarms[i].recurrence) {
                    case ALARM_SINGLE:
                    case ALARM_DAILY:
                        dayMatches = true;
                        break;
                    case ALARM_WEEKDAYS: if (currentDayOfWeek >= 1 && currentDayOfWeek <= 5) dayMatches = true; break;
                    case ALARM_WEEKENDS: if (currentDayOfWeek == 0 || currentDayOfWeek == 6) dayMatches = true; break;
                }
                if (dayMatches) {
                    shouldRing = true;
                    if (appState.alarms[i].recurrence == ALARM_SINGLE) {
                        appState.alarms[i].active = false;
                        (*savesOut)++;
                    }
                }
            }
        }
    }
    return shouldRing;
}

// alarm.cpp: calcolo snooze
inline void snooze(int hour, int minute, uint8_t snoozeTime, int* nextH, int* nextM) {
    int snoozeMins = hour * 60 + minute + snoozeTime;
    *nextH = (snoozeMins / 60) % 24;
    *nextM = snoozeMins % 60;
}

// led.cpp: led_on(), duty finale
inline uint32_t ledOnDuty(uint8_t brightness_1_to_10) {
    if (brightness_1_to_10 > 10) brightness_1_to_10 = 10;
    if (brightness_1_to_10 < 1) brightness_1_to_10 = 1;
    float t = (brightness_1_to_10 - 1.0f) / 9.0f;
    float curve = pow(t, 2.0f);
    uint32_t target_duty = round(5.0f + (250.0f * curve));
    return target_duty;
}

// alarm.cpp: max_duty della rampa
inline uint8_t alarmMaxDuty(uint8_t ledBrightness) {
    uint8_t max_duty = 0;
    if (ledBrightness > 0) {
        float t_bri = (ledBrightness - 1.0f) / 9.0f;
        float curve_bri = pow(t_bri, 2.0f);
        max_duty = round(5.0f + (250.0f * curve_bri));
    }
    return max_duty;
}

// alarm.cpp: duty della rampa a un dato istante
inline uint8_t alarmFade(uint8_t max_duty, int64_t elapsedUs) {
    const int64_t halfCycleTimeUs = 6000000LL;
    const int64_t cycleTimeUs = halfCycleTimeUs * 2;
    int64_t timeInCycleUs = elapsedUs % cycleTimeUs;
    float fadeIndex = 0.0f;
    if (timeInCycleUs < halfCycleTimeUs) {
        fadeIndex = (float)timeInCycleUs / (float)halfCycleTimeUs;
    } else {
        int64_t timeInRampDown = timeInCycleUs - halfCycleTimeUs;
        fadeIndex = 1.0f - ((float)timeInRampDown / (float)halfCycleTimeUs);
    }
    uint8_t duty = round((float)max_duty * fadeIndex); // led_set_duty(uint8_t)
    return duty;
}

// audio.cpp: setVolume(), guadagno
inline float volumeGain(uint8_t volume) {
    if (volume < 1) volume = 1;
    if (volume > 10) volume = 10;
    float t = (volume - 1.0f) / 9.0f;
    float curve = pow(t, 3.0f);
    float gain = 0.03f + (curve * 0.8f);
    return gain;
}

// main.cpp: loop(), calcolo di sleep_ms
inline uint32_t loopSleepMs(int64_t now_us, int64_t lastActivityTime, const SystemState& appState, bool ledState) {
    const int64_t INACTIVITY_TIMEOUT_US = 30000000LL;
    const uint32_t portMAX_DELAY = 0xFFFFFFFF;
    uint32_t sleep_ms = portMAX_DELAY;
    if (appState.currentScreen != SCREEN_HOME || ledState || appState.bluetoothEnabled) {
        int64_t next_timeout_us = lastActivityTime + INACTIVITY_TIMEOUT_US;
        if (appState.currentScreen == SCREEN_HOME && ledState) {
            next_timeout_us = lastActivityTime + ((int64_t)appState.ledAutoOffTime * 1000000LL);
        }
        if (appState.bluetoothEnabled && appState.currentScreen == SCREEN_HOME && !appState.bluetoothConnected) {
            int64_t bt_timeout = lastActivityTime + 300000000LL;
            if (bt_timeout < next_timeout_us) next_timeout_us = bt_timeout;
        }
        if (next_timeout_us > now_us) {
            sleep_ms = (next_timeout_us - now_us) / 1000;
        } else {
            sleep_ms = 0;
        }
    } else {
        int64_t next_timeout_us = lastActivityTime + INACTIVITY_TIMEOUT_US;
        if (next_timeout_us > now_us) {
            sleep_ms = (next_timeout_us - now_us) / 1000;
        } else {
            sleep_ms = 0;
        }
    }
    if (sleep_ms > 5000) sleep_ms = 5000;
    return sleep_ms;
}

// ui.cpp: ui_update(), decisione di ridisegno
struct Ui { bool needsUpdate; bool exitingMenu; bool screenChanged; };
inline Ui uiNeedsUpdate(const SystemState& newState, const SystemState& lastDrawnState, bool forceFullRefresh) {
    bool needsUpdate = forceFullRefresh;
    bool exitingMenu = false;
    bool screenChanged = false;
    if (newState.currentScreen != lastDrawnState.currentScreen) {
        needsUpdate = true;
        if (lastDrawnState.currentScreen == SCREEN_MENU && newState.currentScreen == SCREEN_HOME) {
            exitingMenu = true;
        }
        screenChanged = true;
    }
    if (newState.currentScreen == SCREEN_HOME) {
        if (newState.minute != lastDrawnState.minute ||
            abs(newState.temperature - lastDrawnState.temperature) >= 0.5 ||
            newState.batteryPercent != lastDrawnState.batteryPercent ||
            newState.day != lastDrawnState.day ||
            newState.nextAlarmFound != lastDrawnState.nextAlarmFound ||
            newState.nextAlarmHour != lastDrawnState.nextAlarmHour ||
            newState.nextAlarmMinute != lastDrawnState.nextAlarmMinute ||
            newState.ntpSynced != lastDrawnState.ntpSynced) {
            needsUpdate = true;
        }
    } else if (newState.currentScreen == SCREEN_MENU) {
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
            newState.numConfiguredAlarms != lastDrawnState.numConfiguredAlarms ||
            newState.alarmVolume != lastDrawnState.alarmVolume ||
            newState.snoozeTime != lastDrawnState.snoozeTime ||
            newState.autoStopTime != lastDrawnState.autoStopTime ||
            newState.alarmsGloballyEnabled != lastDrawnState.alarmsGloballyEnabled ||
            newState.ledBrightness != lastDrawnState.ledBrightness ||
            newState.ledAutoOffTime != lastDrawnState.ledAutoOffTime ||
            newState.audioError != lastDrawnState.audioError ||
            newState.isPreviewPlaying != lastDrawnState.isPreviewPlaying ||
            abs(newState.batteryVoltage - lastDrawnState.batteryVoltage) > 0.01 ||
            newState.currentTrackIndex != lastDrawnState.currentTrackIndex ||
            newState.selectedFontIndex != lastDrawnState.selectedFontIndex ||
            newState.ntpDisabled != lastDrawnState.ntpDisabled ||
            strcmp(newState.wifiStatus, lastDrawnState.wifiStatus) != 0 ||
            newState.bluetoothEnabled != lastDrawnState.bluetoothEnabled ||
            newState.bluetoothConnected != lastDrawnState.bluetoothConnected ||
            strcmp(newState.bluetoothDeviceName, lastDrawnState.bluetoothDeviceName) != 0 ||
            newState.pairedDevicesCount != lastDrawnState.pairedDevicesCount ||
            newState.lastNtpSyncTime != lastDrawnState.lastNtpSyncTime ||
            alarmsChanged) {
            needsUpdate = true;
        }
    } else if (newState.currentScreen == SCREEN_ALARM) {
        if (newState.minute != lastDrawnState.minute) {
            needsUpdate = true;
        }
    }
    return {needsUpdate, exitingMenu, screenChanged};
}

// input.cpp: evaluate_input_state(), con le static raccolte in una struct
struct InputState {
    int64_t _pressTimeA_us = 0, _pressTimeB_us = 0, _comboStartTime_us = 0;
    bool _stateA = false, _stateB = false;
    bool _handledA = false, _handledB = false, _comboHandled = false, _inCombo = false;
    int64_t LONG_PRESS_US = 0;
    const int64_t DEBOUNCE_US = 30000;

    InputEvent evaluate(bool rawA, bool rawB, int64_t now_us) {
        if (rawA && !_stateA) { _stateA = true; _pressTimeA_us = now_us; _handledA = _inCombo; }
        if (rawB && !_stateB) { _stateB = true; _pressTimeB_us = now_us; _handledB = _inCombo; }
        if (!rawA) {
            if (_stateA && !_handledA && (now_us - _pressTimeA_us < DEBOUNCE_US)) {
                _handledA = true;
            }
            _stateA = false;
        }
        if (!rawB) {
            if (_stateB && !_handledB && (now_us - _pressTimeB_us < DEBOUNCE_US)) {
                _handledB = true;
            }
            _stateB = false;
        }
        if (_stateA && _stateB) {
            if (!_inCombo) {
                _inCombo = true;
                _comboStartTime_us = now_us;
                _comboHandled = false;
                _handledA = true;
                _handledB = true;
            } else if (!_comboHandled && (now_us - _comboStartTime_us >= LONG_PRESS_US)) {
                _comboHandled = true;
                return INPUT_BTN_AB_LONG;
            }
        } else {
            if (_inCombo) {
                if (!rawA && !rawB) {
                    _inCombo = false;
                    if (!_comboHandled && (now_us - _comboStartTime_us >= DEBOUNCE_US)) {
                        _comboHandled = true;
                        return INPUT_BTN_AB_SHORT;
                    }
                }
            }
        }
        if (_inCombo || (rawA && _handledA) || (rawB && _handledB)) return INPUT_NONE;
        if (_stateA) {
            if (!_handledA && (now_us - _pressTimeA_us >= LONG_PRESS_US)) {
                _handledA = true;
                return INPUT_BTN_A_LONG;
            }
        } else {
            if (!_handledA && _pressTimeA_us > 0 && (now_us - _pressTimeA_us >= DEBOUNCE_US)) {
                _handledA = true;
                _pressTimeA_us = 0;
                return INPUT_BTN_A_SHORT;
            }
        }
        if (_stateB) {
            if (!_handledB && (now_us - _pressTimeB_us >= LONG_PRESS_US)) {
                _handledB = true;
                return INPUT_BTN_B_LONG;
            }
        } else {
            if (!_handledB && _pressTimeB_us > 0 && (now_us - _pressTimeB_us >= DEBOUNCE_US)) {
                _handledB = true;
                _pressTimeB_us = 0;
                return INPUT_BTN_B_SHORT;
            }
        }
        if (!_stateA && _handledA) _pressTimeA_us = 0;
        if (!_stateB && _handledB) _pressTimeB_us = 0;
        return INPUT_NONE;
    }

    // Condizione di sleep di input_task()
    bool idle() const {
        return !_stateA && !_stateB && (_pressTimeA_us == 0) && (_pressTimeB_us == 0) && !_inCombo;
    }
};

} // namespace legacy

#endif // LEGACY_LOGIC_H
