#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <GxEPD2_BW.h> // Aggiungiamo GxEPD2 qui per poter esportare l'oggetto display

// Esportiamo l'oggetto display per poterlo usare in UI
extern GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display;

// Funzione per inizializzare il display e-Paper
void initDisplay();

// Esegue un aggiornamento completo del display
void displayFullRefresh();

// Esegue un aggiornamento parziale del display
void displayPartialRefresh();

// Scrive un testo di prova sul display
void displayTestText(const char* text);

#endif // DISPLAY_H
