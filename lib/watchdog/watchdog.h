#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <Arduino.h>

// Inizializza il watchdog del task principale con timeout configurabile.
void watchdog_init();

// Feed del watchdog: va chiamato periodicamente nei loop critici.
void watchdog_feed();

#endif // WATCHDOG_H
