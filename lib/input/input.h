#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>

// Definizione degli stati possibili
enum InputEvent {
    INPUT_NONE = 0,
    INPUT_BTN_A_SHORT,
    INPUT_BTN_B_SHORT,
    INPUT_BTN_AB_SHORT,
    INPUT_BTN_A_LONG,
    INPUT_BTN_B_LONG,
    INPUT_BTN_AB_LONG,
    INPUT_RTC_WAKEUP // Nuovo evento hardware
};

// Inizializza i pin assegnati ai pulsanti e all'RTC (avvia il task RTOS)
void input_init(uint8_t pinA, uint8_t pinB, uint8_t pinRtc, unsigned long longPressMs);

// Legge un evento in modo asincrono o bloccante
// timeout_ms: 0 = non bloccante, portMAX_DELAY = bloccante infinito, altrimenti attesa max in ms
InputEvent input_get_event(uint32_t timeout_ms);

#endif // INPUT_H