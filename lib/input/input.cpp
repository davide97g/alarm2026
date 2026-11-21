#include "input.h"
#include "../logic/logic.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static uint8_t _pinA;
static uint8_t _pinB;
static ButtonFsm fsm; // Logica short/long/combo/debounce (lib/logic, testata su host)

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

// Task RTOS in background
void input_task(void *pvParameters) {
    while (1) {
        // Se non stiamo tracciando alcuna pressione (tutto fermo), dormiamo in attesa dell'ISR
        if (fsm.idle()) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        bool rawA = (digitalRead(_pinA) == LOW);
        bool rawB = (digitalRead(_pinB) == LOW);
        InputEvent evt = fsm.step(rawA, rawB, esp_timer_get_time());
        if (evt != INPUT_NONE) {
            xQueueSend(appEventQueue, &evt, 0);
        }

        // Svegli (qualcuno sta premendo): polling a 50Hz, reattivo ma leggero sulla CPU
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void input_init(uint8_t pinA, uint8_t pinB, uint8_t pinRtc, unsigned long longPressMs) {
    _pinA = pinA;
    _pinB = pinB;
    fsm.longPressUs = (int64_t)longPressMs * 1000;

    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    pinMode(pinRtc, INPUT_PULLUP);

    appEventQueue = xQueueCreate(10, sizeof(InputEvent));

    // Crea il task pinned al Core 1 (stesso loop di default Arduino) con priorità elevata
    xTaskCreatePinnedToCore(input_task, "InputTask", 2048, NULL, 5, &inputTaskHandle, 1);

    // Attacca gli interrupt (CHANGE per bottoni, FALLING per RTC)
    attachInterrupt(digitalPinToInterrupt(_pinA), btn_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinB), btn_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pinRtc), rtc_isr, FALLING);
}

InputEvent input_get_event(uint32_t timeout_ms) {
    InputEvent evt = INPUT_NONE;
    TickType_t xTicksToWait = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    if (xQueueReceive(appEventQueue, &evt, xTicksToWait) == pdPASS) {
        return evt;
    }
    return INPUT_NONE;
}
