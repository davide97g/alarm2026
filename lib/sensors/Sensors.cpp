#include "Sensors.h"

// Creazione degli oggetti globali per i sensori
RTC_DS3231 rtc;
Adafruit_BME280 bme;
bool bmeValid = false;
bool rtcValid = false;

void initSensors() {
    // 1. Inizializza BME280 (Indirizzo I2C tipico 0x76 o 0x77)
    // Passiamo &Wire per assicurarci che usi il bus I2C che abbiamo aperto nel main
    if (!bme.begin(0x76, &Wire)) {
        Serial.println("[SENSOR] Errore: Sensore BME280 non trovato. Controlla i cavi I2C!");
        bmeValid = false;
    } else {
        Serial.println("[SENSOR] BME280 inizializzato con successo.");
        bmeValid = true;
    }

    // 2. Inizializza RTC DS3231
    if (!rtc.begin(&Wire)) {
        Serial.println("[SENSOR] Errore: Modulo RTC DS3231 non trovato!");
        rtcValid = false;
    } else {
        Serial.println("[SENSOR] RTC DS3231 inizializzato con successo.");
        rtcValid = true;
        // Se l'orologio ha perso energia, impostiamo l'ora di compilazione del codice
        if (rtc.lostPower()) {
            Serial.println("[SENSOR] L'RTC ha perso l'alimentazione, imposto l'ora di default!");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
        
        // Disabilita l'onda quadra sul pin SQW/INT e lo trasforma nel pin di trigger degli allarmi
        rtc.writeSqwPinMode(DS3231_OFF);

        // Pulisce preventivamente eventuali allarmi rimasti "appesi" al riavvio
        rtc.clearAlarm(1);
        rtc.clearAlarm(2);
    }
}

void printEnvironment() {
    Serial.print("Temperatura: ");
    Serial.print(bme.readTemperature());
    Serial.println(" *C");

    Serial.print("Umidita': ");
    Serial.print(bme.readHumidity());
    Serial.println(" %");
    Serial.println("----------------------");
}

void printTime() {
    DateTime now = rtc.now();
    Serial.print("Data e Ora: ");
    Serial.print(now.day(), DEC);    Serial.print('/');
    Serial.print(now.month(), DEC);  Serial.print('/');
    Serial.print(now.year(), DEC);   Serial.print("  ");
    Serial.print(now.hour(), DEC);   Serial.print(':');
    Serial.print(now.minute(), DEC); Serial.print(':');
    Serial.print(now.second(), DEC); Serial.println("\n----------------------");
}

// --- Gestione Allarmi RTC (DS3231) ---

void rtc_setupAlarm1_EveryMinute() {
    rtc.disableAlarm(1);
    rtc.clearAlarm(1);
    DateTime alarmTime(2000, 1, 1, 0, 0, 0); 
    rtc.setAlarm1(alarmTime, DS3231_A1_Second);
}

void rtc_setupAlarm2(uint8_t hour, uint8_t minute) {
    rtc.disableAlarm(2);
    rtc.clearAlarm(2);
    DateTime alarmTime(2000, 1, 1, hour, minute, 0);
    rtc.setAlarm2(alarmTime, DS3231_A2_Hour);
}

bool rtc_isAlarm1Fired() {
    return rtc.alarmFired(1);
}

bool rtc_isAlarm2Fired() {
    return rtc.alarmFired(2);
}

void rtc_clearAlarm1() {
    rtc.clearAlarm(1);
}

void rtc_clearAlarm2() {
    rtc.clearAlarm(2);
}