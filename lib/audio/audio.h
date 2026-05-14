#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include "../../include/config.h"

// Inizializza il filesystem, il modulo I2S e il pin SD_MODE
void initAudio();

// Flag globale per lo stato di inizializzazione
extern bool audioOk;

// Array in RAM per contenere le forme d'onda calcolate (50 barre visive per ogni traccia)
extern uint8_t trackWaveforms[NUM_TRACKS][50];

// Imposta il volume di riproduzione (1-10)
void setVolume(uint8_t volume);

// Attiva o disattiva manualmente l'amplificatore (tramite pin SD_MODE)
void setAudioEnabled(bool enable);

// Avvia la riproduzione di un file MP3 presente nella partizione LittleFS
// Es: playMp3("/traccia.mp3");
bool playMp3(const char* filename);

// Ferma la riproduzione in corso e spegne l'amplificatore
void stopAudio();

// Da chiamare costantemente all'interno del loop() principale di main.cpp
// Ritorna true se la traccia sta suonando, false se è finita o ferma.
bool loopAudio();

// Abilita o disabilita il ricevitore Bluetooth A2DP spegnendo temporaneamente il lettore MP3
void toggleBluetooth(bool enable, const char* deviceName);

#endif // AUDIO_H
