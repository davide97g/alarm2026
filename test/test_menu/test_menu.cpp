// Test del menù (lib/menu/menu.cpp) e del salvataggio in NVS (StateManager).
//  1. Scenari d'uso leggibili.
//  2. Confronto differenziale: la stessa sequenza casuale di pulsanti viene data
//     al menù nuovo e alla copia congelata dell'originale (test/legacy). Stato,
//     scritture NVS, output seriale ed effetti (LED, audio, BT, RTC, UI) devono
//     coincidere passo per passo.
#include <unity.h>
#include <random>
#include <vector>
#include <string>
#include "Arduino.h"
#include "../../lib/state/StateManager.cpp"
#include "../../lib/logic/logic.cpp"
#include "../../lib/menu/menu.cpp"

// --- Riferimento: menu.cpp originale nel namespace legacy -------------------
// La rinomina evita l'ambiguità con ::triggerManualNtpSync (ricerca ADL su SystemState)
#define triggerManualNtpSync legacy_triggerManualNtpSync
namespace legacy {
#include "../legacy/menu_original.inc"
}
#undef triggerManualNtpSync

// --- Finti condivisi ----------------------------------------------------------
FakeSerial Serial;
int64_t fake_now_us = 0;
FakeNvs default_nvs;
FakeNvs* fake_nvs = &default_nvs;

static std::string trace; // Effetti collaterali in ordine

// Copione di un passo: cosa risponde l'hardware finto
struct Script {
    bool playOk = true;
    int loops = 0;                 // quante volte loopAudio() ritorna true
    int keyAfter = -1;             // dopo quanti input_get_event(0) arriva un tasto (-1 = mai)
    InputEvent key = INPUT_BTN_B_SHORT;
};
static Script script;
static int inputPolls = 0;

static void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    trace += buf;
    trace += "\n";
}

// Globali di main.cpp / audio.cpp per il menù nuovo...
bool ledState = false;
int64_t lastActivityTime = 0;
char trackNames[NUM_TRACKS][64];
uint8_t actualNumTracks = 0;
// ...e per la copia originale
namespace legacy {
bool ledState = false;
int64_t lastActivityTime = 0;
char trackNames[NUM_TRACKS][64];
uint8_t actualNumTracks = 0;
}

void scheduleScreenUpdate() { note("scheduleScreenUpdate"); }
void triggerManualNtpSync(SystemState& s) {
    note("ntpSync");
    strncpy(s.wifiStatus, "✓ OK", sizeof(s.wifiStatus) - 1);
    s.ntpSynced = true;
}
void toggleBluetooth(bool enable, const char* name) { note("bt %d %s", enable, name); }
void calculateNextAlarm(SystemState& s) {
    if (!s.alarmsGloballyEnabled) { s.isSnoozeActive = false; s.nextAlarmFound = false; }
    else {
        s.nextAlarmFound = false;
        for (int i = 0; i < s.numConfiguredAlarms; i++) s.nextAlarmFound |= s.alarms[i].active;
    }
    note("calcNext n=%d snooze=%d found=%d", s.numConfiguredAlarms, s.isSnoozeActive, s.nextAlarmFound);
}
void ui_update(const SystemState& s, bool full) {
    note("ui screen=%d tab=%d item=%d preview=%d err=%d full=%d", s.currentScreen, s.menuCursorIndex,
         s.menuItemIndex, s.isPreviewPlaying, s.audioError, full);
}
void led_on(uint8_t b, bool fade) { note("led_on %d %d", b, fade); }
void led_off(bool fade) { note("led_off %d", fade); }
void setVolume(uint8_t v) { note("volume %d", v); }
bool playMp3(const char* f) { note("play %s -> %d", f, script.playOk); return script.playOk; }
bool loopAudio() { return script.loops-- > 0; }
void stopAudio() { note("stopAudio"); }
void watchdog_feed() {}
InputEvent input_get_event(uint32_t) {
    return (script.keyAfter >= 0 && inputPolls++ >= script.keyAfter) ? script.key : INPUT_NONE;
}

namespace legacy {
void scheduleScreenUpdate() { ::scheduleScreenUpdate(); }
void legacy_triggerManualNtpSync(SystemState& s) { ::triggerManualNtpSync(s); }
void toggleBluetooth(bool e, const char* n) { ::toggleBluetooth(e, n); }
}

void setUp() {}
void tearDown() {}

// --- Mondo: tutto lo stato osservabile di un'implementazione ------------------
struct World {
    SystemState s;
    bool led = false;
    int64_t last = 0;
    FakeNvs nvs;
};

struct Outcome { std::string serial, trace; };

static Outcome step(World& w, InputEvent ev, const Script& sc, int64_t now, bool useLegacy) {
    fake_nvs = &w.nvs;
    Serial.out.clear();
    trace.clear();
    script = sc;
    inputPolls = 0;
    fake_now_us = now;
    if (useLegacy) {
        legacy::ledState = w.led;
        legacy::lastActivityTime = w.last;
        legacy::handleMenu(w.s, ev);
        w.led = legacy::ledState;
        w.last = legacy::lastActivityTime;
    } else {
        ::ledState = w.led;
        ::lastActivityTime = w.last;
        handleMenu(w.s, ev);
        w.led = ::ledState;
        w.last = ::lastActivityTime;
    }
    return {Serial.out, trace};
}

static void setTracks(int n) {
    actualNumTracks = legacy::actualNumTracks = n;
    for (int i = 0; i < NUM_TRACKS; i++) {
        snprintf(trackNames[i], 64, "/track%d.mp3", i);
        memcpy(legacy::trackNames[i], trackNames[i], 64);
    }
}

static World freshWorld() {
    World w;
    memset(&w.s, 0, sizeof(w.s));
    fake_nvs = &w.nvs;
    stateManager.begin();
    stateManager.loadAll(w.s);
    w.nvs.writes.clear();
    return w;
}

// --- 1. Scenari ---------------------------------------------------------------

static void test_enter_and_leave_menu() {
    setTracks(3);
    World w = freshWorld();
    step(w, INPUT_BTN_AB_LONG, {}, 0, false);
    TEST_ASSERT_EQUAL(SCREEN_MENU, w.s.currentScreen);
    step(w, INPUT_BTN_AB_LONG, {}, 0, false);
    TEST_ASSERT_EQUAL(SCREEN_HOME, w.s.currentScreen);
}

static void test_home_button_toggles_light() {
    setTracks(3);
    World w = freshWorld();
    Outcome o = step(w, INPUT_BTN_A_SHORT, {}, 0, false);
    TEST_ASSERT_TRUE(w.led);
    TEST_ASSERT_EQUAL_STRING("led_on 5 1\nscheduleScreenUpdate\n", o.trace.c_str());
    step(w, INPUT_BTN_B_SHORT, {}, 0, false);
    TEST_ASSERT_FALSE(w.led);
}

static void test_volume_wraps_both_ways_and_is_saved() {
    setTracks(3);
    World w = freshWorld();
    w.s.currentScreen = SCREEN_MENU;
    w.s.menuItemIndex = 1; // Volume
    w.s.alarmVolume = 10;
    step(w, INPUT_BTN_B_SHORT, {}, 0, false);
    TEST_ASSERT_EQUAL(1, w.s.alarmVolume);
    TEST_ASSERT_EQUAL_UINT64(1, w.nvs.values["sveglia/vol"]);
    step(w, INPUT_BTN_B_LONG, {}, 0, false);
    TEST_ASSERT_EQUAL(10, w.s.alarmVolume);
}

static void test_add_edit_and_delete_alarm() {
    setTracks(3);
    World w = freshWorld();
    w.s.currentScreen = SCREEN_MENU;
    step(w, INPUT_BTN_AB_SHORT, {}, 0, false); // scheda Sveglie, riga "+"
    TEST_ASSERT_EQUAL(1, w.s.menuCursorIndex);
    step(w, INPUT_BTN_B_SHORT, {}, 0, false);  // aggiunge 07:00 e entra in modifica
    TEST_ASSERT_EQUAL(1, w.s.numConfiguredAlarms);
    TEST_ASSERT_TRUE(w.s.isEditingAlarm);
    TEST_ASSERT_EQUAL(7, w.s.alarms[0].hour);
    step(w, INPUT_BTN_B_SHORT, {}, 0, false);  // ora +1
    TEST_ASSERT_EQUAL(8, w.s.alarms[0].hour);
    step(w, INPUT_BTN_B_LONG, {}, 0, false);   // ora -1
    TEST_ASSERT_EQUAL(7, w.s.alarms[0].hour);
    step(w, INPUT_BTN_A_SHORT, {}, 0, false);  // campo minuti
    step(w, INPUT_BTN_B_LONG, {}, 0, false);   // -5 min
    TEST_ASSERT_EQUAL(55, w.s.alarms[0].minute);
    TEST_ASSERT_EQUAL_UINT64(55, w.nvs.values["sveglia/a0_m"]);
    step(w, INPUT_BTN_A_SHORT, {}, 0, false);
    step(w, INPUT_BTN_A_SHORT, {}, 0, false);  // campo cestino
    step(w, INPUT_BTN_B_SHORT, {}, 0, false);  // elimina
    TEST_ASSERT_EQUAL(0, w.s.numConfiguredAlarms);
    TEST_ASSERT_FALSE(w.s.isEditingAlarm);
}

static void test_track_preview_stops_on_key() {
    setTracks(3);
    World w = freshWorld();
    w.s.currentScreen = SCREEN_MENU;
    w.s.menuCursorIndex = 2;
    w.s.menuItemIndex = 1;
    Script sc;
    sc.loops = 1000;
    sc.keyAfter = 3;
    Outcome o = step(w, INPUT_BTN_B_SHORT, sc, 0, false);
    TEST_ASSERT_EQUAL(1, w.s.currentTrackIndex);
    TEST_ASSERT_FALSE(w.s.isPreviewPlaying);
    TEST_ASSERT_NOT_NULL(strstr(o.serial.c_str(), "Anteprima interrotta"));
    TEST_ASSERT_NOT_NULL(strstr(o.trace.c_str(), "play /track1.mp3"));
}

// --- 2. Differenziale -----------------------------------------------------------

static void test_matches_original_on_random_button_sequences() {
    std::mt19937 rng(2026);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    long steps = 0;
    for (int seed = 0; seed < 4000; seed++) {
        setTracks(rnd(0, NUM_TRACKS));
        World a = freshWorld();
        // Stato iniziale casuale ma raggiungibile
        SystemState& s = a.s;
        s.currentScreen = (ScreenType)rnd(0, 2);
        s.alarmVolume = rnd(1, 10);
        s.snoozeTime = rnd(1, 20);
        s.autoStopTime = rnd(1, 5);
        s.ledBrightness = rnd(0, 10);
        s.ledAutoOffTime = rnd(1, 6) * 5;
        s.selectedFontIndex = rnd(0, 2);
        s.alarmsGloballyEnabled = rnd(0, 1);
        s.ntpDisabled = rnd(0, 1);
        s.bluetoothEnabled = rnd(0, 1);
        s.isSnoozeActive = rnd(0, 1);
        s.numConfiguredAlarms = rnd(0, 16);
        for (auto& al : s.alarms) al = {(uint8_t)rnd(0, 23), (uint8_t)(rnd(0, 11) * 5), (bool)rnd(0, 1), (AlarmRecurrence)rnd(0, 3)};
        s.menuCursorIndex = rnd(0, 3);
        uint8_t count = menuItemCount(s, actualNumTracks);
        s.menuItemIndex = count ? rnd(0, count - 1) : 0;
        s.isEditingAlarm = s.menuCursorIndex == 1 && s.menuItemIndex < s.numConfiguredAlarms && rnd(0, 1);
        s.alarmEditFieldIndex = rnd(0, 3);
        a.led = rnd(0, 1);
        World b = a; // stessa partenza, NVS compresa

        for (int i = 0; i < 60; i++, steps++) {
            InputEvent ev = (InputEvent)rnd(1, 7);
            Script sc;
            sc.playOk = rnd(0, 4) != 0;
            sc.loops = rnd(0, 3) == 0 ? rnd(7000, 9000) : rnd(0, 20); // a volte oltre il timeout di 15 s
            sc.keyAfter = rnd(0, 2) == 0 ? -1 : rnd(0, 30);
            sc.key = (InputEvent)rnd(1, 6);
            int64_t now = (int64_t)(seed * 1000 + i) * 1000000LL;

            Outcome oa = step(a, ev, sc, now, true);
            Outcome ob = step(b, ev, sc, now, false);

            char where[96];
            snprintf(where, sizeof(where), "seed %d, step %d, event %d", seed, i, ev);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(oa.serial.c_str(), ob.serial.c_str(), where);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(oa.trace.c_str(), ob.trace.c_str(), where);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&a.s, &b.s, sizeof(SystemState), where);
            TEST_ASSERT_EQUAL_MESSAGE(a.led, b.led, where);
            TEST_ASSERT_TRUE_MESSAGE(a.last == b.last, where);
            TEST_ASSERT_TRUE_MESSAGE(a.nvs.values == b.nvs.values, where);
            TEST_ASSERT_TRUE_MESSAGE(a.nvs.writes == b.nvs.writes, where);
        }
    }
    TEST_ASSERT_EQUAL(240000, steps);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_enter_and_leave_menu);
    RUN_TEST(test_home_button_toggles_light);
    RUN_TEST(test_volume_wraps_both_ways_and_is_saved);
    RUN_TEST(test_add_edit_and_delete_alarm);
    RUN_TEST(test_track_preview_stops_on_key);
    RUN_TEST(test_matches_original_on_random_button_sequences);
    return UNITY_END();
}
