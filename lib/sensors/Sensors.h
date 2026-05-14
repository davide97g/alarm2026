#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>           // Libreria per RTC DS3231
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>  // Libreria per sensore BME280

extern RTC_DS3231 rtc;
extern Adafruit_BME280 bme;
extern bool bmeValid;
extern bool rtcValid;

void initSensors();
void printEnvironment();
void printTime();

// --- Gestione Allarmi RTC (DS3231) ---
void rtc_setupAlarm1_EveryMinute();
void rtc_setupAlarm2(uint8_t hour, uint8_t minute);
bool rtc_isAlarm1Fired();
bool rtc_isAlarm2Fired();
void rtc_clearAlarm1();
void rtc_clearAlarm2();

#endif