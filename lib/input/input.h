#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>

// Definizione dei 6 stati possibili dei pulsanti
enum InputEvent {
    INPUT_NONE = 0,
    INPUT_BTN_A_SHORT,
    INPUT_BTN_B_SHORT,
    INPUT_BTN_AB_SHORT,
    INPUT_BTN_A_LONG,
    INPUT_BTN_B_LONG,
    INPUT_BTN_AB_LONG
};

// Inizializza i pin assegnati ai pulsanti (abilita le resistenze PULLUP)
void input_init(uint8_t pinA, uint8_t pinB, unsigned long longPressMs);

// Da chiamare costantemente nel loop() per leggere lo stato hardware
// Ritorna l'evento rilevato (o INPUT_NONE se non è successo nulla)
InputEvent input_update();

#endif // INPUT_H