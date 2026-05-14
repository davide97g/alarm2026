#include "display.h"
#include "../../include/config.h"
#include "../watchdog/watchdog.h"
#include "../audio/audio.h"
#include <SPI.h>

// Inclusione GxEPD2 e font Adafruit
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>

// Inizializzazione display per Waveshare 5.79" (792x272) B/W (Driver GDEY0579T93)
GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(GxEPD2_579_GDEY0579T93(/*CS=*/ EPD_CS, /*DC=*/ EPD_DC, /*RST=*/ EPD_RST, /*BUSY=*/ EPD_BUSY));

// Callback hardware: eseguita continuamente dal driver mentre aspetta il refresh del pannello.
// Previene il balbettio dell'audio MP3 durante i lenti aggiornamenti dell'e-paper.
static void epdBusyCallback(const void* p) {
    watchdog_feed();
    if (audioOk) {
        loopAudio();
    }
}

void initDisplay() {
    // Gestione power se necessario (ad es. VCC controllato via EPD_PWR)
    // (I pin sono ora pre-configurati in main.cpp prima del rilascio dell'hold per evitare micro-interruzioni)
    pinMode(EPD_PWR, OUTPUT);
    digitalWrite(EPD_PWR, HIGH); 
    delay(10); // Ridotto da 100 a 10 per non rallentare inutilmente il boot

    // Il pin CS DEVE essere impostato a -1, altrimenti il driver hardware SPI combatte contro GxEPD2
    SPI.begin(EPD_SCK, -1, EPD_MOSI, -1); 

    // Registra la callback PRIMA dell'init
    display.epd2.setBusyCallback(epdBusyCallback);

    // Inizializza GxEPD2 una sola volta all'avvio.
    // Grazie al Light Sleep, non ci saranno riavvii e la libreria manterrà il suo stato.
    display.init(115200, true, 10, false);
    
    // Impostazioni base
    display.setRotation(0); // 0 = landscape (792x272)
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);
}

void displayFullRefresh() {
    display.init(115200, true, 10, false);
    display.setFullWindow();
    display.firstPage();
    do {
        watchdog_feed();
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(10, 30);
        display.print("E-Paper 5.79\" Full Refresh completato!");
    } while (display.nextPage());
    // Rimosso hibernate() per coerenza con la conservazione della RAM
}

void displayPartialRefresh() {
    // Init senza reset hardware per preservare la RAM (come facciamo in ui.cpp)
    display.init(115200, false, 0, false);

    // Sottratti 8 pixel in larghezza per prevenire il force-full-refresh di alcuni driver
    display.setPartialWindow(0, 0, display.width() - 8, display.height());
    display.firstPage();
    do {
        watchdog_feed();
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(10, 60);
        display.print("Aggiornamento Parziale (Partial Refresh)");
    } while (display.nextPage());
    // Rimosso hibernate()
}

void displayTestText(const char* text) {
    display.init(115200, true, 2, false);
    display.setFullWindow();
    display.firstPage();
    do {
        watchdog_feed();
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(10, 100);
        display.print(text);
    } while (display.nextPage());
    display.hibernate();
}
