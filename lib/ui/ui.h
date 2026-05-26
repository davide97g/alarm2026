#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include "../../include/config.h" // Inclusione delle costanti centralizzate


#include "../state/SystemState.h"

// Funzione di inizializzazione della logica UI
void ui_init();

// Funzione principale del front-end.
// Riceve in input lo stato aggiornato dai sensori e decide:
// 1. Se è necessario aggiornare lo schermo
// 2. Se eseguire un aggiornamento parziale o totale
// forceFullRefresh forzerà l'aggiornamento totale se impostato a 'true'
void ui_update(const SystemState& newState, bool forceFullRefresh = false);

#endif // UI_H