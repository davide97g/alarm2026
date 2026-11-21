// Test del salvataggio impostazioni (StateManager su NVS).
// I nomi delle chiavi sono un contratto: se cambiano, dopo un aggiornamento
// firmware la sveglia perde impostazioni e sveglie salvate.
#include <unity.h>
#include "Arduino.h"
#include "../../lib/state/StateManager.cpp"

FakeSerial Serial;
int64_t fake_now_us = 0;
FakeNvs nvs;
FakeNvs* fake_nvs = &nvs;

void setUp() { nvs = FakeNvs(); stateManager.begin(); }
void tearDown() {}

static void test_defaults_on_empty_nvs() {
    SystemState s;
    stateManager.loadAll(s);
    TEST_ASSERT_EQUAL(1950, s.batAdcMin);
    TEST_ASSERT_EQUAL(2580, s.batAdcMax);
    TEST_ASSERT_EQUAL(800, s.longPressMs);
    TEST_ASSERT_EQUAL(7, s.alarmVolume);
    TEST_ASSERT_EQUAL(5, s.snoozeTime);
    TEST_ASSERT_EQUAL(3, s.autoStopTime);
    TEST_ASSERT_TRUE(s.alarmsGloballyEnabled);
    TEST_ASSERT_EQUAL(5, s.ledBrightness);
    TEST_ASSERT_EQUAL(10, s.ledAutoOffTime);
    TEST_ASSERT_EQUAL(0, s.numConfiguredAlarms);
    TEST_ASSERT_EQUAL(SCREEN_HOME, s.currentScreen);
    TEST_ASSERT_EQUAL(255, s.lastBatteryHour);
    TEST_ASSERT_EQUAL(-1, s.lastSyncDay);
    TEST_ASSERT_EQUAL_STRING("✗ Non sincron.", s.wifiStatus);
    TEST_ASSERT_EQUAL_STRING("Nessuno", s.bluetoothDeviceName);
    TEST_ASSERT_EQUAL(7, s.alarms[15].hour);
    TEST_ASSERT_EQUAL(ALARM_SINGLE, s.alarms[15].recurrence);
}

static void test_nvs_key_names_are_stable() {
    SystemState s;
    stateManager.loadAll(s);
    stateManager.setAlarmVolume(s, 9);
    stateManager.setSnoozeTime(s, 12);
    stateManager.setAutoStopTime(s, 4);
    stateManager.setAlarmsGloballyEnabled(s, false);
    stateManager.setLedBrightness(s, 3);
    stateManager.setLedAutoOffTime(s, 25);
    stateManager.setFontIndex(s, 2);
    stateManager.setTrackIndex(s, 6);
    stateManager.setNtpDisabled(s, true);
    stateManager.setNtpSynced(s, true, 1767225600);
    stateManager.setBluetoothEnabled(s, true);
    const char* keys[] = {"vol", "snooze", "autostop", "al_global", "led_bri", "led_time", "font_idx",
                          "track_idx", "ntp_disabled", "ntp_synced", "last_ntp", "bt_enabled"};
    for (const char* k : keys) {
        TEST_ASSERT_TRUE_MESSAGE(nvs.values.count(std::string("sveglia/") + k), k);
    }
    s.numConfiguredAlarms = 2;
    s.alarms[1] = {6, 45, true, ALARM_WEEKENDS};
    stateManager.saveAllAlarms(s);
    TEST_ASSERT_EQUAL_UINT64(2, nvs.values["sveglia/num_alarms"]);
    TEST_ASSERT_EQUAL_UINT64(6, nvs.values["sveglia/a1_h"]);
    TEST_ASSERT_EQUAL_UINT64(45, nvs.values["sveglia/a1_m"]);
    TEST_ASSERT_EQUAL_UINT64(1, nvs.values["sveglia/a1_act"]);
    TEST_ASSERT_EQUAL_UINT64(ALARM_WEEKENDS, nvs.values["sveglia/a1_rec"]);
    TEST_ASSERT_EQUAL(12 + 1 + 16 * 4, (int)nvs.writes.size()); // 12 scritture dei setter + num_alarms + 16 slot
}

static void test_round_trip_after_reboot() {
    SystemState s;
    stateManager.loadAll(s);
    stateManager.setAlarmVolume(s, 2);
    stateManager.setLedAutoOffTime(s, 30);
    s.numConfiguredAlarms = 3;
    for (uint8_t i = 0; i < 3; i++) s.alarms[i] = {(uint8_t)(5 + i), (uint8_t)(10 * i), i != 1, (AlarmRecurrence)i};
    stateManager.saveAllAlarms(s);

    SystemState r; // "riavvio": stato nuovo letto dalla stessa NVS
    stateManager.loadAll(r);
    TEST_ASSERT_EQUAL(2, r.alarmVolume);
    TEST_ASSERT_EQUAL(30, r.ledAutoOffTime);
    TEST_ASSERT_EQUAL(3, r.numConfiguredAlarms);
    TEST_ASSERT_EQUAL_MEMORY(s.alarms, r.alarms, sizeof(r.alarms));
}

static void test_delete_alarm_shifts_the_rest() {
    SystemState s;
    stateManager.loadAll(s);
    s.numConfiguredAlarms = 3;
    for (uint8_t i = 0; i < 3; i++) s.alarms[i] = {(uint8_t)(6 + i), 0, true, ALARM_DAILY};
    stateManager.deleteAlarm(s, 0);
    TEST_ASSERT_EQUAL(2, s.numConfiguredAlarms);
    TEST_ASSERT_EQUAL(7, s.alarms[0].hour);
    TEST_ASSERT_EQUAL(8, s.alarms[1].hour);
    TEST_ASSERT_EQUAL_UINT64(7, nvs.values["sveglia/a0_h"]);
    stateManager.deleteAlarm(s, 5); // fuori range: nessun effetto
    TEST_ASSERT_EQUAL(2, s.numConfiguredAlarms);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_on_empty_nvs);
    RUN_TEST(test_nvs_key_names_are_stable);
    RUN_TEST(test_round_trip_after_reboot);
    RUN_TEST(test_delete_alarm_shifts_the_rest);
    return UNITY_END();
}
