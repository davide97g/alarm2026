#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// Tenta di connettersi al WiFi, sincronizza l'orario tramite server NTP 
// e quindi spegne immediatamente il modulo WiFi per risparmiare energia.
// Ritorna true se la sincronizzazione ha avuto successo.
// timeout_ms: timeout totale per la sincronizzazione (default 10 secondi)
bool syncTimeNTP(const char* ssid, const char* password, unsigned long timeout_ms = 10000);

// Ritorna una stringa descrittiva dell'ultimo errore WiFi basato sullo stato attuale
// Utile per visualizzare nell'UI lo stato della sincronizzazione NTP
const char* getWifiErrorMessage();

#endif // NETWORK_H