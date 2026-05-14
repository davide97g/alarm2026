#include "watchdog.h"
#include "../../include/config.h"
#include <esp_task_wdt.h>

void watchdog_init() {
    // Il watchdog del task principale viene inizializzato per evitare lock in loop critici.
    // Se il task principale resta bloccato oltre WATCHDOG_TIMEOUT_SECONDS, l'ESP32 si riavvia.
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true);
    esp_task_wdt_add(NULL);
}

void watchdog_feed() {
    esp_task_wdt_reset();
}
