#ifndef MENU_H
#define MENU_H

#include "../ui/ui.h"  // Per SystemState
#include "../input/input.h"  // Per InputEvent

// Gestisce la navigazione e modifica del menu
void handleMenu(SystemState &appState, InputEvent event);

#endif