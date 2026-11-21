// Test della macchina a stati dei pulsanti (ButtonFsm): scenari reali
// + confronto con evaluate_input_state() originale su sequenze casuali.
#include <unity.h>
#include <random>
#include <vector>
#include "../../lib/logic/logic.cpp"
#include "../legacy/legacy_logic.h"

void setUp() {}
void tearDown() {}

static const int64_t LONG_US = 800000; // LONG_PRESS_MS_DEFAULT

// Simula input_task(): un campione ogni 20 ms mentre i tasti sono "tracciati"
struct Bench {
    ButtonFsm fsm;
    int64_t now = 1000000;
    std::vector<InputEvent> events;
    Bench() { fsm.longPressUs = LONG_US; }
    void hold(bool a, bool b, int64_t ms) {
        for (int64_t t = 0; t < ms; t += 20) {
            InputEvent e = fsm.step(a, b, now);
            if (e != INPUT_NONE) events.push_back(e);
            now += 20000;
        }
    }
};

static void test_short_press_a() {
    Bench b;
    b.hold(true, false, 100);
    b.hold(false, false, 100);
    TEST_ASSERT_EQUAL(1, b.events.size());
    TEST_ASSERT_EQUAL(INPUT_BTN_A_SHORT, b.events[0]);
    TEST_ASSERT_TRUE(b.fsm.idle());
}

static void test_long_press_b_fires_once_while_held() {
    Bench b;
    b.hold(false, true, 2000);
    b.hold(false, false, 100);
    TEST_ASSERT_EQUAL(1, b.events.size());
    TEST_ASSERT_EQUAL(INPUT_BTN_B_LONG, b.events[0]);
}

static void test_combo_short_and_long() {
    Bench s;
    s.hold(true, true, 200);
    s.hold(false, false, 100);
    TEST_ASSERT_EQUAL(1, s.events.size());
    TEST_ASSERT_EQUAL(INPUT_BTN_AB_SHORT, s.events[0]);

    Bench l;
    l.hold(true, true, 1500);
    l.hold(false, false, 100);
    TEST_ASSERT_EQUAL(1, l.events.size());
    TEST_ASSERT_EQUAL(INPUT_BTN_AB_LONG, l.events[0]);
}

static void test_bounce_shorter_than_debounce_is_ignored() {
    ButtonFsm f;
    f.longPressUs = LONG_US;
    TEST_ASSERT_EQUAL(INPUT_NONE, f.step(true, false, 1000000));
    TEST_ASSERT_EQUAL(INPUT_NONE, f.step(false, false, 1010000)); // 10 ms < 30 ms
    TEST_ASSERT_EQUAL(INPUT_NONE, f.step(false, false, 1100000));
    TEST_ASSERT_TRUE(f.idle());
}

// Stessa sequenza grezza alle due implementazioni: eventi e stato devono coincidere
static void test_matches_original_on_random_sequences() {
    std::mt19937 rng(7);
    for (int seq = 0; seq < 3000; seq++) {
        ButtonFsm n;
        legacy::InputState o;
        n.longPressUs = o.LONG_PRESS_US = (int64_t)std::uniform_int_distribution<int>(100, 3000)(rng) * 1000;
        int64_t now = std::uniform_int_distribution<int>(0, 5)(rng) * 1000;
        bool a = false, b = false;
        for (int step = 0; step < 400; step++) {
            int r = std::uniform_int_distribution<int>(0, 9)(rng);
            if (r == 0) a = !a;
            if (r == 1) b = !b;
            if (r == 2) { a = !a; b = !b; }
            // Passi brevi (rimbalzi) e lunghi (pressioni prolungate)
            now += std::uniform_int_distribution<int>(0, 1)(rng) ? std::uniform_int_distribution<int>(1, 40000)(rng)
                                                                 : std::uniform_int_distribution<int>(1, 1500000)(rng);
            TEST_ASSERT_EQUAL(o.evaluate(a, b, now), n.step(a, b, now));
            TEST_ASSERT_EQUAL(o.idle(), n.idle());
            TEST_ASSERT_EQUAL(o._pressTimeA_us, n.pressTimeA);
            TEST_ASSERT_EQUAL(o._pressTimeB_us, n.pressTimeB);
            TEST_ASSERT_EQUAL(o._inCombo, n.inCombo);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_short_press_a);
    RUN_TEST(test_long_press_b_fires_once_while_held);
    RUN_TEST(test_combo_short_and_long);
    RUN_TEST(test_bounce_shorter_than_debounce_is_ignored);
    RUN_TEST(test_matches_original_on_random_sequences);
    return UNITY_END();
}
