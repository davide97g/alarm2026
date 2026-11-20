#ifndef INPUT_EVENTS_H
#define INPUT_EVENTS_H

// Eventi prodotti dal task input (senza dipendenze Arduino: usato anche dai test)
enum InputEvent {
    INPUT_NONE = 0,
    INPUT_BTN_A_SHORT,
    INPUT_BTN_B_SHORT,
    INPUT_BTN_AB_SHORT,
    INPUT_BTN_A_LONG,
    INPUT_BTN_B_LONG,
    INPUT_BTN_AB_LONG,
    INPUT_RTC_WAKEUP // Evento hardware
};

#endif // INPUT_EVENTS_H
