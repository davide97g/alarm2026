#include "input.h"

static uint8_t _pinA;
static uint8_t _pinB;

static int64_t _pressTimeA_us = 0;
static int64_t _pressTimeB_us = 0;
static int64_t _comboStartTime_us = 0;

static bool _stateA = false;
static bool _stateB = false;

static bool _handledA = false;
static bool _handledB = false;
static bool _comboHandled = false;
static bool _inCombo = false;

static int64_t LONG_PRESS_US; // Microsecondi per un "Long Press" (configurabile)
const int64_t DEBOUNCE_US = 30000;    // Microsecondi per il Debounce (30ms)
const int64_t COMBO_TOLERANCE_US = 50000; // Finestra utile per premere entrambi (50ms)

void input_init(uint8_t pinA, uint8_t pinB, unsigned long longPressMs) {
    _pinA = pinA;
    _pinB = pinB;
    LONG_PRESS_US = (int64_t)longPressMs * 1000;
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
}

InputEvent input_update() {
    bool rawA = (digitalRead(_pinA) == LOW);
    bool rawB = (digitalRead(_pinB) == LOW);
    int64_t now_us = esp_timer_get_time();

    // Rileva momento di pressione (Transizione LOW)
    if (rawA && !_stateA) { _stateA = true; _pressTimeA_us = now_us; _handledA = _inCombo; }
    if (rawB && !_stateB) { _stateB = true; _pressTimeB_us = now_us; _handledB = _inCombo; }
    
    // Rileva momento di rilascio (Transizione HIGH)
    if (!rawA) { 
        if (_stateA && !_handledA && (now_us - _pressTimeA_us < DEBOUNCE_US)) {
            _handledA = true; // Ignora il rimbalzo meccanico (pressione troppo breve)
        }
        _stateA = false; 
    }
    if (!rawB) { 
        if (_stateB && !_handledB && (now_us - _pressTimeB_us < DEBOUNCE_US)) {
            _handledB = true; // Ignora il rimbalzo meccanico (pressione troppo breve)
        }
        _stateB = false; 
    }

    // --- LOGICA COMBO (A + B) ---
    if (_stateA && _stateB) {
        if (!_inCombo) {
            _inCombo = true;
            _comboStartTime_us = now_us;
            _comboHandled = false;
            _handledA = true; // Annulla la pressione singola in corso
            _handledB = true; 
        } else if (!_comboHandled && (now_us - _comboStartTime_us >= LONG_PRESS_US)) {
            _comboHandled = true;
            return INPUT_BTN_AB_LONG;
        }
    } else {
        if (_inCombo) {
            // Se eravamo in combo e rilasciamo almeno un tasto
            if (!rawA && !rawB) {
                _inCombo = false;
                if (!_comboHandled && (now_us - _comboStartTime_us >= DEBOUNCE_US)) {
                    _comboHandled = true;
                    return INPUT_BTN_AB_SHORT;
                }
            }
        }
    }

    // Se stiamo gestendo un combo (o abbiamo appena finito), blocca letture singole
    if (_inCombo || (rawA && _handledA) || (rawB && _handledB)) {
        return INPUT_NONE;
    }

    // --- LOGICA SINGOLA (A) ---
    if (_stateA) {
        // Pressione prolungata A
        if (!_handledA && (now_us - _pressTimeA_us >= LONG_PRESS_US)) {
            _handledA = true;
            return INPUT_BTN_A_LONG;
        }
    } else {
        // Rilascio corto A (solo se è durato più del debounce ma meno del long press)
        if (!_handledA && _pressTimeA_us > 0 && (now_us - _pressTimeA_us >= DEBOUNCE_US)) {
            _handledA = true;
            _pressTimeA_us = 0;
            return INPUT_BTN_A_SHORT;
        }
    }

    // --- LOGICA SINGOLA (B) ---
    if (_stateB) {
        // Pressione prolungata B
        if (!_handledB && (now_us - _pressTimeB_us >= LONG_PRESS_US)) {
            _handledB = true;
            return INPUT_BTN_B_LONG;
        }
    } else {
        // Rilascio corto B
        if (!_handledB && _pressTimeB_us > 0 && (now_us - _pressTimeB_us >= DEBOUNCE_US)) {
            _handledB = true;
            _pressTimeB_us = 0;
            return INPUT_BTN_B_SHORT;
        }
    }

    // Se si entra in questo stato e _pressTime > 0 e non siamo in debounce, svuotiamo i registri per evitare false trigger
    if (!_stateA && _handledA) _pressTimeA_us = 0;
    if (!_stateB && _handledB) _pressTimeB_us = 0;

    return INPUT_NONE;
}