#include "network.h"
#include "../../include/config.h"
#include "../watchdog/watchdog.h"  // Aggiunto per watchdog_feed()

// Variabile globale per memorizzare l'ultimo stato/errore WiFi
static uint8_t lastWifiStatus = WL_IDLE_STATUS;
static bool lastNtpSuccess = false;

const uint8_t STATUS_NTP_TIMEOUT = 100; // Stato custom per il timeout del server

// Spegne la radio (risparmio energetico) e ricorda l'esito per getWifiErrorMessage()
static bool finish(uint8_t status, bool success) {
    lastWifiStatus = status;
    lastNtpSuccess = success;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return success;
}

bool syncTimeNTP(const char* ssid, const char* password, unsigned long timeout_ms) {
    Serial.printf("[NTP] Tentativo di sincronizzazione NTP (timeout=%ums)...\n", timeout_ms);
    Serial.printf("[NTP] Connessione alla rete WiFi: %s\n", ssid);
    
    // 1. Accende esplicitamente la radio WiFi e pulisce connessioni pendenti
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    // Disabilita la persistenza per evitare di salvare le credenziali in flash se non richiesto
    WiFi.persistent(false); 

    WiFi.begin(ssid, password);
    
    // Attende la connessione (con timeout configurabile)
    int64_t startTimeUs = esp_timer_get_time();
    while (WiFi.status() != WL_CONNECTED && (esp_timer_get_time() - startTimeUs) < (timeout_ms * 1000LL)) {
        watchdog_feed();  // Alimenta il watchdog durante l'attesa connessione WiFi
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[NTP] ERRORE: WiFi non connesso dopo %lldms. Stato finale: %d\n", (esp_timer_get_time() - startTimeUs) / 1000, WiFi.status());
        return finish(WiFi.status(), false);
    }

    int64_t wifiConnectedTimeUs = esp_timer_get_time();
    int64_t elapsedForWifi_ms = (wifiConnectedTimeUs - startTimeUs) / 1000;
    Serial.printf("[NTP] WiFi connesso con successo (%lldms)\n", elapsedForWifi_ms);
    lastWifiStatus = WL_CONNECTED;
    
    // Calcola il timeout residuo per la parte NTP, assicurandosi che sia almeno 1 secondo.
    long remaining_timeout_ms = (long)timeout_ms - (long)elapsedForWifi_ms;
    if (remaining_timeout_ms <= 1000) {
        Serial.printf("[NTP] ERRORE: Timeout esaurito (%ldms) durante la connessione WiFi. Impossibile procedere con NTP.\n", remaining_timeout_ms);
        return finish(WL_CONNECT_FAILED, false);
    }

    // ATTESA STRATEGICA: Il router ha assegnato l'IP, ma il NAT (Internet) potrebbe 
    // richiedere ancora 1-2 secondi per aprirsi. Se mandiamo la richiesta UDP istantaneamente, 
    // va persa e l'ESP32 fa un "backoff" bloccandosi per svariati secondi.
    Serial.println("[NTP] Rete agganciata. Attesa 1.5s per stabilizzazione routing...");
    delay(1500);

    // --- FIX CRITICO: Falso Positivo Sincronizzazione ---
    // L'ESP32 in Light Sleep sballa l'orario interno (es. 18 min in 5 giorni).
    // Resettiamo forzatamente l'orologio al 1970. In questo modo getLocalTime()
    // non restituirà l'orario sballato, ma sarà obbligato ad aspettare il server NTP!
    
    struct timeval old_tv;
    gettimeofday(&old_tv, NULL); // Salva l'orario (anche se derivato) prima di azzerarlo
    int64_t preSyncTimerUs = esp_timer_get_time(); // Timestamp per compensare l'attesa

    struct timeval tv = {0, 0};
    settimeofday(&tv, NULL);

    // Forza l'avvio o il riavvio del client NTP in questo momento esatto
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", NTP_SERVER);

    // Sicurezza Watchdog: Limitiamo la chiamata bloccante a 10s per prevenire riavvii forzati
    if (remaining_timeout_ms > 10000) remaining_timeout_ms = 10000;

    struct tm timeinfo;
    Serial.printf("[NTP] Attesa dell'orario tramite server NTP (timeout %ldms)...\n", remaining_timeout_ms);
    
    // Attende di ricevere l'orario (timeout configurabile da chiamante)
    int64_t ntpStartUs = esp_timer_get_time();
    watchdog_feed();  // Alimenta watchdog prima della chiamata bloccante NTP
    // Usa il timeout RESIDUO per la chiamata a getLocalTime.
    // Questa è una chiamata bloccante, il watchdog non viene nutrito durante l'attesa.
    // È FONDAMENTALE che remaining_timeout_ms sia < WATCHDOG_TIMEOUT_SECONDS.
    if (!getLocalTime(&timeinfo, (uint32_t)remaining_timeout_ms)) {
        // RESTORE: Se l'NTP fallisce, ripristina l'orario salvato compensando i secondi trascorsi
        old_tv.tv_sec += (esp_timer_get_time() - preSyncTimerUs) / 1000000LL;
        settimeofday(&old_tv, NULL);

        watchdog_feed();  // Alimenta watchdog dopo timeout NTP
        int64_t elapsedMs = (esp_timer_get_time() - ntpStartUs) / 1000;
        Serial.printf("[NTP] ERRORE: Timeout nella ricezione orario NTP (%lldms)\n", elapsedMs);
        return finish(STATUS_NTP_TIMEOUT, false); // Timeout NTP distinto dall'errore WiFi
    }
    watchdog_feed();  // Alimenta watchdog dopo successo NTP

    Serial.printf("[NTP] Orario ricevuto: %04d-%02d-%02d %02d:%02d:%02d (%lldms)\n",
                 // FIX: Aggiunto +1900 per l'anno e +1 per il mese, come in altre parti del codice
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, (esp_timer_get_time() - ntpStartUs) / 1000);
    
    finish(WL_CONNECTED, true);
    Serial.println("[NTP] Sincronizzazione NTP completata. Modulo WiFi spento.");
    return true;
}

// Ritorna una stringa descrittiva dello stato WiFi/NTP
const char* getWifiErrorMessage() {
    if (lastNtpSuccess) {
        return "✓ OK";
    }
    
    // Mappe gli stati WiFi a messaggi descrittivi
    switch (lastWifiStatus) {
        case WL_NO_SSID_AVAIL:
            return "✗ Rete non trovata";
        case WL_CONNECT_FAILED:
            return "✗ Password sbagliata";
        case WL_CONNECTION_LOST:
            return "✗ Connessione persa";
        case WL_DISCONNECTED:
            return "✗ Disconnesso";
        case WL_IDLE_STATUS:
            return "✗ Non sincron.";
        case STATUS_NTP_TIMEOUT:
            return "✗ Timeout server NTP";
        default:
            return "✗ Errore rete";
    }
}