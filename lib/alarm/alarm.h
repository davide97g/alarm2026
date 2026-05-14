#ifndef ALARM_H
#define ALARM_H

#include "../ui/ui.h"  // Per SystemState

// Gestisce il ciclo bloccante di suoneria sveglia
void handleAlarm(SystemState &appState);

// Calcola la prossima sveglia e la invia all'RTC
void calculateNextAlarm(SystemState &appState);

#endif