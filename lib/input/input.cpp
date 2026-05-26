#include "input.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static uint8_t _pinA;
static uint8_t _pinB;
static uint8_t _pinRtc;

static int64_t _pressTimeA_us = 0;
static int64_t _pressTimeB_us = 0;
static int64_t _comboStartTime_us = 0;

static bool _stateA = false;
static bool _stateB = false;

static bool _handledA = false;
static bool _handledB = false;
static bool _comboHandled = false;
static bool _inCombo = false;

static int64_t LONG_PRESS_US;
const int64_t DEBOUNCE_US = 30000;
const int64_t COMBO_TOLERANCE_US = 50000;

// Variabili RTOS
static QueueHandle_t appEventQueue = NULL;
static TaskHandle_t inputTaskHandle = NULL;

// ISR (Interrupt Service Routines)
void IRAM_ATTR btn_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(inputTaskHandle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void IRAM_ATTR rtc_isr() {
    InputEvent evt = INPUT_RTC_WAKEUP;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(appEventQueue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// Logica originaria di valutazione (ma isolata dal loop principale)
InputEvent evaluate_input_state() {
    bool rawA = (digitalRead(_pinA) == LOW);
    bool rawB = (digitalRead(_pinB) == LOW);
    int64_t now_us = esp_timer_get_time();

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

// Task RTOS in background
void input_task(void *pvParameters) {
    while (1) {
        // Se non stiamo tracciando alcuna pressione (tutto fermo), dormiamo in attesa dell'ISR
        if (!_stateA && !_stateB && (_pressTimeA_us == 0) && (_pressTimeB_us == 0) && !_inCombo) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        InputEvent evt = evaluate_input_state();
        if (evt != INPUT_NONE) {
            xQueueSend(appEventQueue, &evt, 0);
        }

        // Se siamo svegli (qualcuno sta premendo), facciamo polling intelligente a 50Hz (20ms di delay)
        // Questo delega l'uso della CPU ma mantiene perfetta reattività e debounce.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void input_init(uint8_t pinA, uint8_t pinB, uint8_t pinRtc, unsigned long longPressMs) {
    _pinA = pinA;
    _pinB = pinB;
    _pinRtc = pinRtc;
    LONG_PRESS_US = (int64_t)longPressMs * 1000;
    
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    pinMode(_pinRtc, INPUT_PULLUP);

    appEventQueue = xQueueCreate(10, sizeof(InputEvent));

    // Crea il task pinned al Core 1 (stesso loop di default Arduino) con priorità elevata
    xTaskCreatePinnedToCore(input_task, "InputTask", 2048, NULL, 5, &inputTaskHandle, 1);

    // Attacca gli interrupt (CHANGE per bottoni, FALLING per RTC)
    attachInterrupt(digitalPinToInterrupt(_pinA), btn_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinB), btn_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinRtc), rtc_isr, FALLING);
}

InputEvent input_get_event(uint32_t timeout_ms) {
    InputEvent evt = INPUT_NONE;
    TickType_t xTicksToWait = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    if (xQueueReceive(appEventQueue, &evt, xTicksToWait) == pdPASS) {
        return evt;
    }
    return INPUT_NONE;
}