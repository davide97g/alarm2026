#include <Arduino.h>
#include <Wire.h> // Necessaria per il bus I2C condiviso da RTC e BME280
#include "Sensors.h" // Inclusione della nostra nuova libreria locale
#include "input.h" // Inclusione del nuovo modulo per la gestione input avanzata
#include "ui.h" // Inclusione del front-end
#include "display.h" // Inclusione gestione display
#include "power.h" // Inclusione della gestione Deep Sleep
#include "audio.h" // Inclusione della gestione Audio I2S
#include "led.h" // Inclusione della gestione LED PWM
#include "network.h" // Inclusione per la sincronizzazione NTP
#include "config.h" // Configurazioni centralizzate (Pin, costanti globali)
#include "watchdog.h" // Gestione watchdog per recovery da blocchi critici
#include <Preferences.h> // Libreria nativa ESP32 per salvare dati persistenti
#include <esp_attr.h> // Necessario per RTC_DATA_ATTR
#include <driver/gpio.h> // Necessario per gpio_hold_en
#include <driver/rtc_io.h> // Necessario per gpio_deep_sleep_hold_en
#include "secrets.h" // Inclusione delle credenziali WiFi sicure
#include "alarm.h" // modulo per gestione allarme
#include "menu.h" // modulo per gestione menu

extern RTC_DS3231 rtc; // Assicura visibilità dell'oggetto rtc per accedere direttamente alle API avanzate
extern Adafruit_BME280 bme; // Assicura visibilità dell'oggetto bme per le letture dirette

extern void toggleBluetooth(bool enable, const char* deviceName);

// --- VARIABILI GLOBALI ---
int64_t lastActivityTime = 0;
const int64_t INACTIVITY_TIMEOUT_US = 30000000LL; // 30 secondi di inattività prima di dormire (in microsecondi)

// Stato globale dell'applicazione (Valori fittizi iniziali)
// I valori verranno sovrascritti da quelli salvati in NVS, e
// la direttiva RTC_DATA_ATTR li mantiene in memoria RAM RTC durante il Deep Sleep
// Nota: Uso l'inizializzazione posizionale aggregata invece dei designatori C++20.
// Questo funge da workaround per un possibile bug del compilatore o un problema dell'ambiente
// di build che causava un errore sull'ordine dei campi, nonostante l'ordine fosse corretto.
RTC_DATA_ATTR SystemState appState = {
  /* .hour, .minute, .day, .month, .year, .dayOfWeek, .temperature, .humidity, .batteryVoltage, .batteryPercent, .batAdcMin, .batAdcMax, .longPressMs */
  12, 30, 1, 1, 2000, 0, 22.5, 45.0, 4.2, 100, BAT_ADC_MIN_DEFAULT, BAT_ADC_MAX_DEFAULT, LONG_PRESS_MS_DEFAULT,
  /* .currentScreen, .menuCursorIndex, .menuItemIndex, .currentTrackIndex, .isEditingAlarm, .alarmEditFieldIndex, .nextAlarmFound, .nextAlarmHour, .nextAlarmMinute */
  SCREEN_HOME, 0, 0, 0, false, 0, false, 255, 255,
  /* .alarmVolume, .snoozeTime, .autoStopTime, .alarmsGloballyEnabled */
  7, 5, 3, true,
  /* .alarms */
  { // Inizializzatore per l'array 'alarms'
    {7, 0, false, ALARM_SINGLE}, {7, 30, false, ALARM_SINGLE}, {8, 0, false, ALARM_SINGLE}, {8, 30, false, ALARM_SINGLE},
    {9, 0, false, ALARM_SINGLE}, {9, 30, false, ALARM_SINGLE}, {10, 0, false, ALARM_SINGLE}, {10, 30, false, ALARM_SINGLE},
    {11, 0, false, ALARM_SINGLE}, {11, 30, false, ALARM_SINGLE}, {12, 0, false, ALARM_SINGLE}, {12, 30, false, ALARM_SINGLE},
    {13, 0, false, ALARM_SINGLE}, {13, 30, false, ALARM_SINGLE}, {14, 0, false, ALARM_SINGLE}, {14, 30, false, ALARM_SINGLE}
  },
  /* .numConfiguredAlarms, .ledBrightness, .ledAutoOffTime, .selectedFontIndex, .isPreviewPlaying, .audioError */
  0, 5, 10, 0, false, false,
  /* .ntpSynced, .ntpDisabled, .wifiStatus, .lastNtpSyncTime */
  false, false, "✗ Non sincron.", 0,
  /* .bluetoothEnabled, .bluetoothConnected, .bluetoothDeviceName, .pairedDevicesCount */
  false, false, "Nessuno", 0
};
bool ledState = false;
RTC_DATA_ATTR int lastSyncDay = -1; // Memorizza l'ultimo giorno in cui è stata fatta la sync NTP
RTC_DATA_ATTR uint32_t lastNtpAttempt = 0; // Timestamp dell'ultimo tentativo NTP (per retry orario)
RTC_DATA_ATTR uint8_t lastBatteryHour = 255; // Ultima ora in cui è stata letta la batteria (255 = mai letta)

// Variabili di stato per la funzione Snooze (persistenti in RAM RTC)
RTC_DATA_ATTR bool isSnoozeActive = false;
RTC_DATA_ATTR uint8_t snoozeTargetHour = 0;
RTC_DATA_ATTR uint8_t snoozeTargetMinute = 0;

// Istanza globale per accedere alla memoria flash
Preferences preferences;

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Motivo Risveglio: Allarme RTC (EXT0)"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Motivo Risveglio: Pulsanti (EXT1)"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Motivo Risveglio: Timer interno"); break;
    default : Serial.printf("Motivo Risveglio: Altro / Avvio normale (%d)\n", wakeup_reason); break;
  }
}

void performWakeupTasks(esp_sleep_wakeup_cause_t wakeup_reason); // Dichiarazione forward

void goToLightSleep() {
  Serial.printf("[SLEEP] Nessuna attività. Entro in Light Sleep... (Temp=%.1fC Batt=%d%%)\n", appState.temperature, appState.batteryPercent);
  // Assicuriamoci che al prossimo risveglio o durante i refresh in background si parta sempre dalla Home
  appState.currentScreen = SCREEN_HOME;

  // Sicurezza: Abilita esplicitamente le resistenze di pull-up interne per i pin di wakeup.
  // Previene i "falsi risvegli" a catena causati da pin fluttuanti durante il Light Sleep.
  // NOTA: Avendo spostato l'RTC sul GPIO 14, possiamo sfruttare in sicurezza il PULLUP interno.
  pinMode(RTC_INT_PIN, INPUT_PULLUP);
  pinMode(BTN_A_PIN, INPUT_PULLUP);
  pinMode(BTN_B_PIN, INPUT_PULLUP);

  // Configuriamo i pin per svegliare l'ESP32 (Light Sleep supporta nativamente ANY_LOW per tutti i pin)
  gpio_wakeup_enable((gpio_num_t)RTC_INT_PIN, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_A_PIN, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BTN_B_PIN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  // Blocca l'esecuzione ed entra in letargo leggero mantenendo in vita la RAM!
  esp_light_sleep_start();

  // === L'ESECUZIONE DEL CODICE RIPRENDE DA QUI AL RISVEGLIO ===
  lastActivityTime = esp_timer_get_time(); // Resetta il timer per evitare loop di sonno immediato
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
  Serial.printf("\n[WAKE] Risveglio da Light Sleep! Causa: %d\n", reason);
  
  performWakeupTasks(reason);
}

// Funzione helper per caricare tutte e 9 le sveglie dalla memoria non volatile
void loadAlarms() {
    appState.numConfiguredAlarms = preferences.getUChar("num_alarms", 0); // Di base, parte pulito a 0.
    for (uint8_t i = 0; i < 16; i++) {
        char key[15]; // Le chiavi Preferences supportano max 15 caratteri
        
        snprintf(key, sizeof(key), "a%d_h", i); // es. "a0_h" per l'ora sveglia 0
        appState.alarms[i].hour = preferences.getUChar(key, appState.alarms[i].hour);
        
        snprintf(key, sizeof(key), "a%d_m", i); // es. "a0_m" per i minuti
        appState.alarms[i].minute = preferences.getUChar(key, appState.alarms[i].minute);
         
        snprintf(key, sizeof(key), "a%d_act", i); // es. "a0_act" per lo stato
        appState.alarms[i].active = preferences.getBool(key, false);
        
        snprintf(key, sizeof(key), "a%d_rec", i); // es. "a0_rec" per la ricorrenza
        appState.alarms[i].recurrence = (AlarmRecurrence)preferences.getUChar(key, ALARM_SINGLE);
    }
}

// Funzione helper per salvare in memoria un singolo slot sveglia (usata in seguito dalla UI)
void saveAllAlarms() {
    preferences.putUChar("num_alarms", appState.numConfiguredAlarms);
    for (uint8_t index = 0; index < 16; index++) {
        char key[15];
        
        snprintf(key, sizeof(key), "a%d_h", index);
        preferences.putUChar(key, appState.alarms[index].hour);
        
        snprintf(key, sizeof(key), "a%d_m", index);
        preferences.putUChar(key, appState.alarms[index].minute);
        
        snprintf(key, sizeof(key), "a%d_act", index);
        preferences.putBool(key, appState.alarms[index].active);
        
        snprintf(key, sizeof(key), "a%d_rec", index);
        preferences.putUChar(key, (uint8_t)appState.alarms[index].recurrence);
    }
}

// Funzione helper per eliminare una sveglia e compattare l'array
void deleteAlarm(uint8_t index) {
    if (index >= appState.numConfiguredAlarms) return;
    
    // Shifta le sveglie successive indietro di 1 posizione
    for (uint8_t i = index; i < appState.numConfiguredAlarms - 1; i++) {
        appState.alarms[i] = appState.alarms[i + 1];
    }
    
    // Pulisci l'ultimo elemento rimasto duplicato per coerenza
    uint8_t lastIdx = appState.numConfiguredAlarms - 1;
    appState.alarms[lastIdx] = {7, 0, false, ALARM_SINGLE};
    
    appState.numConfiguredAlarms--;
    saveAllAlarms();              // Salva la nuova lista in NVS
    calculateNextAlarm(appState); // Ricalcola per aggiornare l'RTC
}

// Variabile per mantenere la memoria dell'ultimo livello di batteria e applicare un po' di hysteresis
static int lastBatteryPercent = -1;

// Funzione per aggiornare solo la batteria (chiamata ogni ora se minuti == 0)
void updateBatteryOnly() {
    uint32_t adc_reading = 0;
    for (int i = 0; i < 100; i++) {
        adc_reading += analogRead(BAT_VOLT_PIN);
    }
    adc_reading /= 100;

    // --- LOGICA DI CALIBRAZIONE BASATA SUI DATI REALI (ADC vs MULTIMETRO) ---
    // Dati reali rilevati e convalidati dal multimetro agli estremi di carica:
    // - 4.21V (4210mV) -> ADC 2400 (Carica completa)
    // - 2.80V (2800mV) -> ADC 1510 (Scarica completa)
    // Questa mappa copre l'intera curva interpolando linearmente con i nuovi estremi definitivi.
    long mapped_mv = map(adc_reading, 1510, 2400, 2800, 4210);

    float voltage = mapped_mv / 1000.0f;

    // Sicurezza: limita il voltaggio a un range plausibile per evitare valori assurdi
    // se la calibrazione è completamente sballata.
    if (voltage < 2.5f) voltage = 2.5f;
    if (voltage > 4.3f) voltage = 4.3f;

    appState.batteryVoltage = voltage;

    // Mappatura non-lineare Voltaggio -> Percentuale per batterie 18650 Li-Ion
    // Interpolazione lineare al singolo 1% tra i punti fissi della curva di scarica
    // Aggiornata in base al range reale: 4.21V (Max) - 2.80V (Cut-off)
    int percent;
    if (mapped_mv >= 4200)      percent = 100;
    else if (mapped_mv >= 4050) percent = map(mapped_mv, 4050, 4200, 90, 100);
    else if (mapped_mv >= 3950) percent = map(mapped_mv, 3950, 4050, 80, 90);
    else if (mapped_mv >= 3850) percent = map(mapped_mv, 3850, 3950, 70, 80);
    else if (mapped_mv >= 3750) percent = map(mapped_mv, 3750, 3850, 60, 70);
    else if (mapped_mv >= 3650) percent = map(mapped_mv, 3650, 3750, 50, 60);
    else if (mapped_mv >= 3550) percent = map(mapped_mv, 3550, 3650, 40, 50);
    else if (mapped_mv >= 3450) percent = map(mapped_mv, 3450, 3550, 30, 40);
    else if (mapped_mv >= 3350) percent = map(mapped_mv, 3350, 3450, 20, 30);
    else if (mapped_mv >= 3150) percent = map(mapped_mv, 3150, 3350, 10, 20);
    else if (mapped_mv >= 2950) percent = map(mapped_mv, 2950, 3150, 5, 10);
    else if (mapped_mv >= 2800) percent = map(mapped_mv, 2800, 2950, 0, 5);
    else                        percent = 0;

    // Hysteresis (anti-jitter) per evitare che il valore sfarfalli es. 45% -> 44% -> 45%
    if (lastBatteryPercent != -1) {
        if (abs(percent - lastBatteryPercent) < 2) {
            percent = lastBatteryPercent;
        }
    }
    lastBatteryPercent = percent;
    appState.batteryPercent = percent;
}
// Funzione per leggere i dati REALI dai sensori e popolare il Modello (appState)
void updateSensorsData(bool fullUpdate = true) {
    uint8_t current_hour_for_battery_check;

    // 1. Lettura Orario Reale
    if (rtcValid) {
        DateTime utc_now = rtc.now(); // L'RTC è impostato su UTC
        time_t utc_epoch = utc_now.unixtime();
        struct tm* local_timeinfo = localtime(&utc_epoch); // Converti in ora locale

        appState.hour = local_timeinfo->tm_hour;
        appState.minute = local_timeinfo->tm_min;
        appState.day = local_timeinfo->tm_mday;
        appState.month = local_timeinfo->tm_mon + 1;
        appState.year = local_timeinfo->tm_year + 1900;
        appState.dayOfWeek = local_timeinfo->tm_wday;
        current_hour_for_battery_check = appState.hour;
    } else {
        // Fallback di emergenza
        appState.hour = 12;
        appState.minute = 0;
        current_hour_for_battery_check = appState.hour;
    }

    // 2. Lettura Batteria ogni ora cambiata (indipendentemente da fullUpdate)
    if (current_hour_for_battery_check != lastBatteryHour) {
        updateBatteryOnly();
        lastBatteryHour = current_hour_for_battery_check;
        Serial.printf("[BATTERY] Aggiornata ogni ora: %d%%\n", appState.batteryPercent);
    }

    if (fullUpdate) {
        // 3. Lettura Sensore Ambientale
        if (bmeValid) {
            float temp = bme.readTemperature();
            float hum = bme.readHumidity();
            appState.temperature = isnan(temp) ? 25.0f : temp;
            appState.humidity = isnan(hum) ? 50.0f : hum;
        } else {
            appState.temperature = 25.0f; // Default fallback
            appState.humidity = 50.0f;    // Default fallback
        }
        Serial.printf("[SENSORS] Temp=%.1fC Umid=%.0f%%\n", appState.temperature, appState.humidity);
    }
}

// Funzione invocata dal menù per forzare la sincronizzazione in tempo reale
void triggerManualNtpSync(SystemState &state) {
    if (state.ntpDisabled) {
        Serial.println("[NTP] Manuale: Sincronizzazione disabilitata da menù.");
        strncpy(state.wifiStatus, "✗ Disabilitata", sizeof(state.wifiStatus) - 1);
        ui_update(state, false);
        return;
    }

    Serial.println("[NTP] Sincronizzazione manuale richiesta dall'utente.");
    strncpy(state.wifiStatus, "↻ Sincro in corso...", sizeof(state.wifiStatus) - 1);
    ui_update(state, false); // Aggiornamento parziale rapido per mostrare il messaggio senza sfarfallare
    if (syncTimeNTP(WIFI_SSID, WIFI_PASSWORD, NTP_INITIAL_TIMEOUT)) {
        struct tm timeinfo;
        // Se syncTimeNTP ha avuto successo, l'orario di sistema è già impostato.
        // Lo recuperiamo senza un timeout aggiuntivo, poiché dovrebbe essere immediatamente disponibile.
        if (getLocalTime(&timeinfo)) {
            time_t local_epoch_time = mktime(&timeinfo); // Converte la struct tm (locale) in time_t (UTC epoch)
            rtc.adjust(DateTime(local_epoch_time)); // Imposta l'RTC con il timestamp UTC
            Serial.println("[NTP] Manuale: RTC sincronizzato con successo.");
            state.ntpSynced = true;
            state.lastNtpSyncTime = local_epoch_time; // Salva il timestamp UTC
            strncpy(state.wifiStatus, "✓ OK", sizeof(state.wifiStatus) - 1);
            preferences.putBool("ntp_synced", true);
            preferences.putULong("last_ntp", state.lastNtpSyncTime);
            updateSensorsData(true);
        } else {
            // Questo blocco else non dovrebbe essere raggiunto se syncTimeNTP ha restituito true.
            // Se succede, indica un errore interno inaspettato.
            Serial.println("[NTP] Manuale: ERRORE INTERNO: getLocalTime fallito dopo syncTimeNTP riuscita. RTC NON AGGIORNATO.");
            strncpy(state.wifiStatus, "✗ Errore Interno", sizeof(state.wifiStatus) - 1);
        }
    } else {
        Serial.println("[NTP] Manuale: Sincronizzazione fallita.");
        strncpy(state.wifiStatus, getWifiErrorMessage(), sizeof(state.wifiStatus) - 1);
    }
    state.wifiStatus[sizeof(state.wifiStatus) - 1] = '\0'; // Sicurezza stringa
    
    ui_update(state, false); // Aggiorna parzialmente per mostrare il risultato finale in modo istantaneo
}

// Funzione per pianificare l'Allarme 1 (Aggiornamento Schermo Normale o Notturno)
void scheduleScreenUpdate() {
    rtc.disableAlarm(1);
    rtc.clearAlarm(1);
    
        rtc.setAlarm1(DateTime(2000, 1, 1, 0, 0, 0), DS3231_A1_Second);
    Serial.println("Refresh display schedulato ogni minuto");
}

void performWakeupTasks(esp_sleep_wakeup_cause_t wakeup_reason) {
  bool wokeUpForMinuteUpdate = false;
  bool wokeUpForAlarm = false;

  // In Light Sleep la causa del pin è ESP_SLEEP_WAKEUP_GPIO
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wakeup_reason == ESP_SLEEP_WAKEUP_GPIO || wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
      if (rtc_isAlarm2Fired()) {
          Serial.println("\n========== >>> SVEGLIA! L'Allarme 2 dell'RTC è scattato! <<< ==========\n");
          
          bool shouldRing = false;
          DateTime utc_now = rtc.now();
          time_t utc_epoch = utc_now.unixtime();
          struct tm* local_timeinfo = localtime(&utc_epoch);
          
          int currentDayOfWeek = local_timeinfo->tm_wday;
          int currentMins = local_timeinfo->tm_hour * 60 + local_timeinfo->tm_min;
          
          // 1. Controllo se è in corso uno Snooze
          if (isSnoozeActive) {
              int snoozeMinsFromMidnight = snoozeTargetHour * 60 + snoozeTargetMinute;
              int diff = abs(currentMins - snoozeMinsFromMidnight);
              if (diff <= 2 || diff >= 1438) {
                  shouldRing = true;
                  isSnoozeActive = false; // Consuma lo snooze (ora sta suonando)
                  Serial.println("[ALARM] Snooze time raggiunto! Suono...");
              }
          }
          
          // 2. Se non era uno Snooze (o orario non combaciava), analizzo l'array base delle sveglie
          if (!shouldRing) {
              for (uint8_t i = 0; i < appState.numConfiguredAlarms; i++) {
                  int alarmMins = appState.alarms[i].hour * 60 + appState.alarms[i].minute;
                  int diff = abs(currentMins - alarmMins);
                  
                  if (appState.alarms[i].active && (diff <= 2 || diff >= 1438)) {
                      bool dayMatches = false;
                      switch (appState.alarms[i].recurrence) {
                          case ALARM_SINGLE:
                          case ALARM_DAILY:
                              dayMatches = true;
                              break;
                          case ALARM_WEEKDAYS: if (currentDayOfWeek >= 1 && currentDayOfWeek <= 5) dayMatches = true; break;
                          case ALARM_WEEKENDS: if (currentDayOfWeek == 0 || currentDayOfWeek == 6) dayMatches = true; break;
                      }
                      
                      if (dayMatches) {
                          shouldRing = true;
                          if (appState.alarms[i].recurrence == ALARM_SINGLE) {
                              appState.alarms[i].active = false;
                              saveAllAlarms();
                              Serial.printf("Sveglia %d (Singola) auto-disattivata.\n", i + 1);
                          }
                      }
                  }
              }
          }

          if (shouldRing) {
              handleAlarm(appState);
              wokeUpForAlarm = true;
          } else {
              Serial.println("[ALARM] Falso allarme (Giorno non corrisponde alla ricorrenza). Proseguo in silenzio.");
          }
          
          rtc_clearAlarm2(); // Resetta il flag dell'RTC per il prossimo ciclo
      }
      
      if (!wokeUpForAlarm && rtc_isAlarm1Fired()) {
          wokeUpForMinuteUpdate = true;
      }
      rtc_clearAlarm1(); // Clear sicuro sull'allarme 1
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
      wokeUpForMinuteUpdate = true;
  }

  Serial.println("[WAKE] Lettura sensori ambientali...");
  updateSensorsData(true); // Forza sempre la lettura completa dei sensori I2C ad ogni risveglio
  Serial.printf("[WAKE] Sensori: Temp=%.1fC Umid=%.0f%% Batt=%d%%\n", appState.temperature, appState.humidity, appState.batteryPercent);

  Serial.println("[WAKE] Calcolando pianificazione aggiornamento schermo...");
  scheduleScreenUpdate();
  
  // --- ROUTINE NOTTURNA (03:30) E RETRY NTP ---
  bool forceFullRefresh = false;
  bool shouldAttemptNtp = false;
  uint32_t currentTimeSeconds = rtc.now().unixtime();
  
  if (!appState.ntpDisabled && !appState.ntpSynced) {
      // Se non si è mai sincronizzato, riprova ma aspetta comunque il normale intervallo di retry per non distruggere la batteria
      shouldAttemptNtp = (currentTimeSeconds - lastNtpAttempt) > (NTP_RETRY_INTERVAL / 1000);
      if (shouldAttemptNtp) Serial.printf("[NTP] Tentativo di sincronizzazione iniziale (timeout %ums)\n", NTP_INITIAL_TIMEOUT);
  } else if (!appState.ntpDisabled && appState.ntpSynced && appState.hour == 3 && appState.minute == 30) {
      shouldAttemptNtp = (currentTimeSeconds - lastNtpAttempt) > (NTP_RETRY_INTERVAL / 1000);
      if (shouldAttemptNtp) {
          Serial.printf("[NTP] Routine notturna (03:30): retry NTP (timeout %ums)\n", NTP_QUICK_TIMEOUT);
      }
  }
  
  if (shouldAttemptNtp) {
      unsigned long ntpTimeout = appState.ntpSynced ? NTP_QUICK_TIMEOUT : NTP_INITIAL_TIMEOUT;
      lastNtpAttempt = currentTimeSeconds;
      
      if (syncTimeNTP(WIFI_SSID, WIFI_PASSWORD, ntpTimeout)) {
          struct tm timeinfo;
          // Se syncTimeNTP ha avuto successo, l'orario di sistema è già impostato.
          // Lo recuperiamo senza un timeout aggiuntivo, poiché dovrebbe essere immediatamente disponibile.
          if (getLocalTime(&timeinfo)) { // Questa chiamata dovrebbe sempre avere successo se syncTimeNTP era true
              time_t local_epoch_time = mktime(&timeinfo); // Converte la struct tm (locale) in time_t (UTC epoch)
              rtc.adjust(DateTime(local_epoch_time)); // Imposta l'RTC con il timestamp UTC
              Serial.println("[NTP] RTC sincronizzato con successo ai server NTP.");
              appState.ntpSynced = true;
              appState.lastNtpSyncTime = local_epoch_time; // Salva il timestamp UTC
              strncpy(appState.wifiStatus, "✓ OK", sizeof(appState.wifiStatus) - 1);
              preferences.putBool("ntp_synced", true);
              preferences.putULong("last_ntp", appState.lastNtpSyncTime);
              lastNtpAttempt = local_epoch_time; // Aggiorna lastNtpAttempt con l'orario sincronizzato
              updateSensorsData(true);
              forceFullRefresh = true;
          } else {
              Serial.println("[NTP] ERRORE INTERNO: getLocalTime fallito dopo syncTimeNTP riuscita. RTC NON AGGIORNATO.");
              strncpy(appState.wifiStatus, "✗ Errore Interno", sizeof(appState.wifiStatus) - 1);
          }
      } else {
          Serial.println("[NTP] Tentativo sincronizzazione fallito (dettagli nel log di network.cpp).");
          strncpy(appState.wifiStatus, getWifiErrorMessage(), sizeof(appState.wifiStatus) - 1);
          // Non forziamo ntpSynced a false: manteniamo in memoria l'ultimo stato di successo!
      }
  } 
  
  // Garantisce il Full Refresh notturno alle 3:30 indipendentemente dall'esito dell'NTP
  if (appState.hour == 3 && appState.minute == 30 && lastSyncDay != appState.day) {
      lastSyncDay = appState.day;
      forceFullRefresh = true;
  }

  // CRITICO UX: Calcola la prossima sveglia PRIMA di aggiornare l'UI, così se 
  // l'utente ha appena premuto Snooze o Stop, lo schermo mostrerà subito il nuovo orario.
  calculateNextAlarm(appState);

  ui_init();
  
  bool isColdBoot = (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);
  ui_update(appState, isColdBoot || forceFullRefresh);


  if (wokeUpForMinuteUpdate) {
      // Se l'utente sta navigando nel menù o ha la luce accesa, non forzare lo sleep immediato
      if (appState.currentScreen == SCREEN_HOME && !ledState) {
          Serial.println("Risveglio minore (cambio minuto): imposto timeout immediato per il Light Sleep.");
          if (!appState.bluetoothEnabled) {
              lastActivityTime = esp_timer_get_time() - INACTIVITY_TIMEOUT_US - 100000LL; 
          }
      } else {
          Serial.println("Cambio minuto durante l'uso: mantengo il timeout di inattività normale.");
      }
  }
}

void scanI2C() {
  Serial.println("\n[I2C SCAN] Scansione del bus I2C in corso...");
  byte error, address;
  int nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("           -> Dispositivo I2C trovato all'indirizzo 0x%02X\n", address);
      nDevices++;
    }
  }
  if (nDevices == 0) {
    Serial.println("[I2C SCAN] ERRORE: Nessun dispositivo I2C trovato.");
    Serial.println("           1. Controlla alimentazione (3.3V/GND) e cablaggio (SDA/SCL).");
    Serial.println("           2. Prova a invertire i cavi SDA e SCL tra loro.");
  } else {
    Serial.printf("[I2C SCAN] Scansione completata. Trovati %d dispositivi.\n", nDevices);
    Serial.println("           Verifica che gli indirizzi corrispondano a quelli attesi (RTC=0x68, BME280=0x76 o 0x77).");
  }
  Serial.println("----------------------------------------------------");
  delay(5000); // Pausa per dare tempo di leggere l'output
}

void setup() {
  // WORKAROUND: Disabilita il Brownout Detector.
  // Questo previene i riavvii causati dai picchi di corrente del WiFi, ma non risolve
  // il problema di alimentazione alla radice. Usare con cautela se il sistema è instabile.
  // Lo inseriamo qui perché il log conferma che il problema è un brownout.
  #include "soc/soc.h"
  #include "soc/rtc_cntl_reg.h"
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Configura la Timezone con stringa TZ (Es: Roma/Europa Centrale)
  // Questo garantisce che l'ora legale/solare cambi istantaneamente da sola anche offline.
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", NTP_SERVER);
  Serial.printf("[SETUP] System timezone configured with TZ String, NTP_SERVER=%s\n", NTP_SERVER);

  Serial.begin(115200);
  Serial.println("Avvio back-end Sveglia...");
  watchdog_init();

  // CRITICO: Configura i pin del display PRIMA di rilasciare l'hold.
  // Se rilasciamo l'hold prima di aver impostato OUTPUT/HIGH, i pin cadono a LOW 
  // per qualche millisecondo, tagliando l'alimentazione e corrompendo la RAM!
  pinMode(EPD_PWR, OUTPUT); digitalWrite(EPD_PWR, HIGH);
  pinMode(EPD_CS, OUTPUT);  digitalWrite(EPD_CS, HIGH);
  pinMode(EPD_RST, OUTPUT); digitalWrite(EPD_RST, HIGH);
  pinMode(EPD_DC, OUTPUT);  digitalWrite(EPD_DC, HIGH); // CRITICO: Impedisce che il pin vada LOW al risveglio

  // Rilascia i pin del display trattenuti durante il Deep Sleep per riprendere la comunicazione SPI
  gpio_hold_dis((gpio_num_t)EPD_RST);
  gpio_hold_dis((gpio_num_t)EPD_CS);
  gpio_hold_dis((gpio_num_t)EPD_DC);
  gpio_hold_dis((gpio_num_t)EPD_PWR);
  gpio_deep_sleep_hold_dis();

  print_wakeup_reason(); // Stampa subito il motivo dell'accensione
  lastActivityTime = esp_timer_get_time(); // Inizializza il timer di attività

  // Inizializza la partizione NVS (Non-Volatile Storage) per i settaggi
  preferences.begin("sveglia", false);
  
  // Carica le impostazioni dalla memoria non volatile
  appState.alarmVolume = preferences.getUChar("vol", 7);
  appState.snoozeTime = preferences.getUChar("snooze", 5);
  appState.autoStopTime = preferences.getUChar("autostop", 3);
  appState.alarmsGloballyEnabled = preferences.getBool("al_global", true);
  appState.ledBrightness = preferences.getUChar("led_bri", 5);
  appState.ledAutoOffTime = preferences.getUChar("led_time", 10);
  appState.selectedFontIndex = preferences.getUChar("font_idx", 0);
  appState.currentTrackIndex = preferences.getUChar("track_idx", 0);
  appState.batAdcMin = preferences.getUShort("bat_min", BAT_ADC_MIN_DEFAULT);
  appState.batAdcMax = preferences.getUShort("bat_max", BAT_ADC_MAX_DEFAULT);
  appState.longPressMs = preferences.getULong("long_press_ms", LONG_PRESS_MS_DEFAULT);
  appState.ntpDisabled = preferences.getBool("ntp_disabled", false);
  appState.ntpSynced = preferences.getBool("ntp_synced", false);
  appState.lastNtpSyncTime = preferences.getULong("last_ntp", 0);
  appState.bluetoothEnabled = preferences.getBool("bt_enabled", false);
  
  Serial.printf("[SETUP] Configurazione caricata - Vol:%d Snooze:%d AutoStop:%d LED_Bri:%d LongPress:%lums NTP:%s\n", 
                appState.alarmVolume, appState.snoozeTime, appState.autoStopTime, 
                appState.ledBrightness, appState.longPressMs,
                appState.ntpDisabled ? "DISAB" : (appState.ntpSynced ? "OK" : "PENDING"));

  // Carica in RAM tutte le 9 sveglie sovrascrivendo i default se presenti in memoria
  loadAlarms();

  // Inizializza il bus I2C (usato da RTC e Sensore Ambientale)
  Wire.begin(I2C_SDA, I2C_SCL);

  // Inizializza i sensori I2C
  Serial.println("[SETUP] Inizializzando sensori I2C (RTC, BME280)...");
  initSensors();
  if (!rtcValid) {
      Serial.println("[CRITICAL ERROR] RTC non rilevato! Avvio procedura safe-mode...");
      
      // Inizializza il display per mostrare l'errore
      initDisplay();
      displayTestText("ERRORE CRITICO:\nModulo RTC non rilevato.\nControllare cablaggio I2C.\nSospensione sistema...");
      
      delay(5000);
      enterDeepSleep(0); // Sospensione definitiva usando la funzione nativa in caso di errore critico
  }

  // Inizializza hardware di Input e Output prima del controllo sveglia
  Serial.println("[SETUP] Inizializzando Input, LED, Audio...");
  input_init(BTN_A_PIN, BTN_B_PIN, appState.longPressMs);
  led_init(LED_PWM_PIN);
  initAudio(); // Inizializza LittleFS, I2S e disattiva l'amplificatore
  initDisplay(); // Inizializza il bus SPI e i pin del display per il flusso normale
  Serial.println("[SETUP] Input, LED, Audio OK");
  
  // Ripristina lo stato del Bluetooth se era rimasto attivo all'ultimo riavvio
  if (appState.bluetoothEnabled) {
      toggleBluetooth(true, "Sveglia Smart");
  }

  // Lettura orario per controllare minuti
    DateTime utc_now = rtc.now();
    time_t utc_epoch = utc_now.unixtime();
    struct tm* local_timeinfo = localtime(&utc_epoch);
    appState.hour = local_timeinfo->tm_hour;
    appState.minute = local_timeinfo->tm_min;
    Serial.printf("[SETUP] Orario RTC (Locale): %02d:%02d:%02d\n", appState.hour, appState.minute, local_timeinfo->tm_sec);

  // La lettura batteria è ora gestita in updateSensorsData ogni ora

  pinMode(BAT_VOLT_PIN, INPUT);
  
  // Configura il pin di interrupt dell'RTC con pull-up per leggerlo anche da svegli
  pinMode(RTC_INT_PIN, INPUT_PULLUP);

  // Esegue le logiche di controllo iniziali come se si fosse appena svegliato da Light Sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  performWakeupTasks(wakeup_reason);

  Serial.println("Setup hardware di base completato. Entro in loop...");
}

void loop() {
  watchdog_feed();
  // Lettura non bloccante dei pulsanti tramite la libreria input
  InputEvent event = input_update();

  if (event != INPUT_NONE) {
    lastActivityTime = esp_timer_get_time(); // Resetta il timer di inattività
    Serial.printf("[INPUT] Evento: %d (Home:%d Menu:%d) ora %02d:%02d\n", event, appState.currentScreen == SCREEN_HOME, appState.currentScreen == SCREEN_MENU, appState.hour, appState.minute);
    
    handleMenu(appState, event); // Passa l'evento al gestore del menu
    
    ui_update(appState);
  }

  // --- GESTIONE BACKGROUND RTC ---
  // Dato che l'ESP32 non entra in Light Sleep se l'utente naviga nel menu o usa il BT,
  // controlliamo l'interrupt fisico dell'RTC per intercettare lo scatto infallibile della sveglia.
  if (digitalRead(RTC_INT_PIN) == LOW) {
      Serial.println("[BACKGROUND] Rilevato segnale RTC (Minuto o Sveglia)!");
      performWakeupTasks(ESP_SLEEP_WAKEUP_GPIO);
  }

  // --- GESTIONE TIMEOUT LED E SLEEP ---
  
  // 1. Controllo spegnimento indipendente del LED (se siamo nella Home)
  if (appState.currentScreen == SCREEN_HOME && ledState) {
      if (esp_timer_get_time() - lastActivityTime > ((int64_t)appState.ledAutoOffTime * 1000000LL)) {
          Serial.printf("[TIMEOUT] LED acceso da troppo tempo. Spegnimento automatico (%ds).\n", appState.ledAutoOffTime);
          ledState = false;
          led_off(true); // Fade sempre attivo
          scheduleScreenUpdate();
          ui_update(appState);
          // Resettiamo il timer di inattività affinché il display
          // rimanga visibile (senza led) per i normali 30 secondi prima del Light Sleep
          lastActivityTime = esp_timer_get_time();
      }
  }

  // 2. Controllo timer di uscita menù e sleep generale (30 secondi)
  if (esp_timer_get_time() - lastActivityTime > INACTIVITY_TIMEOUT_US) {
      // Se eravamo in un menù, forziamo l'uscita alla schermata Home
      if (appState.currentScreen != SCREEN_HOME) {
          Serial.println("[TIMEOUT] Inattività nel menù: ritorno alla Home automatico.");
          appState.currentScreen = SCREEN_HOME;
          appState.isEditingAlarm = false; // Chiude in sicurezza eventuali modifiche in sospeso
          
          ui_update(appState); // Disegna fisicamente l'orologio sul display prima dello sleep
          
          // Resettiamo il timer. Se la luce è accesa, verrà gestita dal suo timeout 
          // naturale della Home (es. 10s) per evitare spegnimenti bruschi e glitch.
          lastActivityTime = esp_timer_get_time();
      }
      else {
          // Gestione energetica con Bluetooth (il loop resta sempre vigile se BT è attivo)
          if (appState.bluetoothEnabled) {
              // Spegnimento automatico del BT dopo 5 min (300 sec) se NON è connesso a nulla
              if (!appState.bluetoothConnected && (esp_timer_get_time() - lastActivityTime > 300000000LL)) {
                  Serial.println("[TIMEOUT] Bluetooth non connesso per 5 minuti. Spegnimento automatico.");
                  appState.bluetoothEnabled = false;
                  preferences.putBool("bt_enabled", false);
                  toggleBluetooth(false, "");
                  ui_update(appState); // Aggiorna graficamente UI per mostrare 'Disattivato'
                  lastActivityTime = esp_timer_get_time(); // Resetta per permettere lo spegnimento al prossimo ciclo
              }
          } else if (appState.currentScreen == SCREEN_HOME && !ledState) {
              goToLightSleep();
          }
      }
  }
}
