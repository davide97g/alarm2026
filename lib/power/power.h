#ifndef POWER_H
#define POWER_H

#include <Arduino.h>

// Funzione per mandare l'ESP32 in Deep Sleep per un determinato numero di secondi
// Configura anche il risveglio tramite il Tasto A (Pin 32)
void enterDeepSleep(uint64_t sleep_seconds);

#endif // POWER_H
