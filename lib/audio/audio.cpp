#include "audio.h"
#include "../../include/config.h"
#include "../watchdog/watchdog.h"
#include <LittleFS.h>
#include <math.h>
#include <memory>
#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <Preferences.h>
#include <string.h>

// Libreria per Bluetooth A2DP Sink
// Disabilitiamo temporaneamente i warning del preprocessore per ignorare 
// l'avviso della libreria che richiede "AudioTools", dato che usiamo l'I2S nativo.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include "BluetoothA2DPSink.h"
#pragma GCC diagnostic pop
#include "../ui/ui.h"
#include <driver/i2s.h>

extern SystemState appState;
extern void ui_update(const SystemState& newState, bool forceFullRefresh);

// Oggetti della libreria ESP8266Audio
AudioGeneratorMP3 *mp3 = nullptr;
std::unique_ptr<AudioFileSourceLittleFS> file;
AudioOutputI2S *out = nullptr;

BluetoothA2DPSink a2dp_sink;

bool audioOk = false;

char trackNames[NUM_TRACKS][64] = {0};
uint8_t actualNumTracks = 0;
uint8_t trackWaveforms[NUM_TRACKS][50] = {0};

// --- HELPER E CLASSI PER ANALISI ONDA AUDIO ---

// Genera una firma numerica univoca basata sul nome del file
uint32_t hashFileName(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

// Un "Finto Amplificatore" che invece di suonare analizza i picchi dell'MP3
class AudioOutputWaveform : public AudioOutput {
public:
    uint8_t* waveform;
    uint32_t sampleCount;
    uint32_t currentBar;
    uint32_t samplesPerBar;
    int32_t currentPeak;
    bool done;
    uint32_t skipSamples; // Salta il pop/click iniziale del decoder

    AudioOutputWaveform(uint8_t* outArray) {
        waveform = outArray;
        memset(waveform, 1, 50); // Default a 1 pixel di altezza
        sampleCount = 0;
        currentBar = 0;
        samplesPerBar = 44100 / 5; // 5 barre al secondo (default)
        currentPeak = 0;
        done = false;
        skipSamples = 44100 / 4; // Salta i primi 250ms di audio
    }

    virtual bool SetRate(int hz) override { 
        samplesPerBar = hz / 5; 
        skipSamples = hz / 4; // Aggiorna i sample da saltare in base al framerate reale
        return true; 
    }
    virtual bool SetBitsPerSample(int bits) override { return true; }
    virtual bool SetChannels(int channels) override { return true; }
    virtual bool begin() override { return true; }
    virtual bool stop() override { return true; }
    
    virtual bool ConsumeSample(int16_t sample[2]) override {
        if (done) return true;

        if (skipSamples > 0) {
            skipSamples--;
            return true; // Ignora il campione rumoroso iniziale
        }

        int32_t mono = (abs(sample[0]) + abs(sample[1])) / 2;
        if (mono > currentPeak) currentPeak = mono;

        sampleCount++;
        if (sampleCount >= samplesPerBar) {
            // Mappa il picco (boostato visivamente a 16384) su un massimo di 18 pixel
            int mapped = (currentPeak * 18) / 16384;
            if (mapped > 18) mapped = 18;
            if (mapped < 1) mapped = 1;
            
            waveform[currentBar] = mapped;
            currentBar++;
            sampleCount = 0;
            currentPeak = 0;
            if (currentBar >= 50) done = true;
        }
        return true;
    }
};


void initAudio() {
    // Configura il pin SD_MODE come output e spegnilo inizialmente
    pinMode(I2S_SD_MODE, OUTPUT);
    digitalWrite(I2S_SD_MODE, LOW);

    // Inizializza LittleFS
    // Il primo argomento 'false' evita la formattazione automatica in caso di fallimento.
    // Il quarto argomento "spiffs" specifica l'etichetta della partizione da montare.
    if (!LittleFS.begin(false, "/littlefs", 10, "spiffs")) {
        Serial.println("[CRITICAL ERROR] Errore mount partizione LittleFS 'spiffs'! (Controlla partitions.csv e platformio.ini)");
        audioOk = false;
        return;
    }

    // Scansione dinamica dei file MP3 presenti su LittleFS
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("[AUDIO] ERRORE: Impossibile aprire la directory root di LittleFS.");
    } else {
        File file = root.openNextFile();
        while (file && actualNumTracks < NUM_TRACKS) {
            if (!file.isDirectory()) {
                String fileName = file.name();
                if (fileName.endsWith(".mp3") || fileName.endsWith(".MP3")) {
                    if (!fileName.startsWith("/")) fileName = "/" + fileName;
                    strncpy(trackNames[actualNumTracks], fileName.c_str(), sizeof(trackNames[0]) - 1);
                    actualNumTracks++;
                }
            }
            file = root.openNextFile();
        }
        // Avviso se il limite di tracce è stato raggiunto e ci sono ancora file
        if (file && actualNumTracks == NUM_TRACKS) {
            Serial.printf("[AUDIO] ATTENZIONE: Raggiunto il limite massimo di %d tracce MP3. Alcuni file potrebbero non essere stati caricati.\n", NUM_TRACKS);
        }
        
        // --- CACHING E GENERAZIONE ONDE VISIVE ---
        Preferences wavePrefs;
        wavePrefs.begin("waves", false);
        
        for (int i = 0; i < actualNumTracks; i++) {
            char key[15];
            // Modificato in wv_ per invalidare la vecchia cache e forzare il ricalcolo dell'onda pulita
            snprintf(key, sizeof(key), "wv_%08X", hashFileName(trackNames[i]));
            
            if (wavePrefs.getBytesLength(key) == 50) {
                // Se l'onda esiste già in memoria NVS, caricala istantaneamente
                wavePrefs.getBytes(key, trackWaveforms[i], 50);
                Serial.printf("[AUDIO] Onda visiva pre-calcolata caricata per: %s\n", trackNames[i]);
            } else {
                // Se il file è nuovo, lo analizziamo senza farlo suonare
                Serial.printf("[AUDIO] Analisi onda in corso per: %s (richiederà 1-2 secondi)...\n", trackNames[i]);
                
                AudioFileSourceLittleFS* tempFile = new AudioFileSourceLittleFS(trackNames[i]);
                AudioOutputWaveform* tempOut = new AudioOutputWaveform(trackWaveforms[i]);
                AudioGeneratorMP3* tempMp3 = new AudioGeneratorMP3();
                
                tempMp3->begin(tempFile, tempOut);
                while (tempMp3->isRunning() && !tempOut->done) {
                    watchdog_feed(); // Previene il reset durante l'elaborazione pesante
                    if (!tempMp3->loop()) break;
                }
                tempMp3->stop();
                wavePrefs.putBytes(key, trackWaveforms[i], 50); // Salva in NVS per le accensioni future
                
                delete tempMp3; delete tempOut; delete tempFile;
            }
        }
        wavePrefs.end();
    }

    if (actualNumTracks > 0) {
        Serial.printf("[AUDIO] Trovati %d file MP3 su LittleFS:\n", actualNumTracks);
        for (int i = 0; i < actualNumTracks; i++) {
            Serial.printf(" - %s\n", trackNames[i]);
        }
    } else {
        Serial.println("[AUDIO] ATTENZIONE: Nessun file MP3 trovato! La sveglia suonerà in silenzio.");
    }

    // Inizializza gli oggetti audio
    out = new AudioOutputI2S();
    // Su ESP32 la libreria mappa i pin di default I2S_NUM_0 su 26, 25, 22.
    // Usiamo SetPinout per impostare i pin personalizzati del tuo cablaggio.
    
#ifndef I2S_DOUT
    #define I2S_DOUT 19 // Fallback di sicurezza se manca in config.h
#endif
    out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    
    mp3 = new AudioGeneratorMP3();
    audioOk = true;
}

void btConnectionCallback(esp_a2d_connection_state_t state, void *ptr) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        Serial.println("[BLUETOOTH] Dispositivo Connesso!");
        appState.bluetoothConnected = true;
        strncpy(appState.bluetoothDeviceName, "Dispositivo Esterno", sizeof(appState.bluetoothDeviceName)-1);
        ui_update(appState, false);
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        Serial.println("[BLUETOOTH] Dispositivo Disconnesso!");
        appState.bluetoothConnected = false;
        ui_update(appState, false);
    }
}

void toggleBluetooth(bool enable, const char* deviceName) {
    if (enable) {
        Serial.println("[AUDIO] Disattivazione MP3 I2S e avvio Bluetooth A2DP...");
        stopAudio(); 
        if (out != nullptr) {
            delete out;
            out = nullptr;
        }
        
        i2s_pin_config_t my_pin_config = {
            .bck_io_num = I2S_BCLK,
            .ws_io_num = I2S_LRC,
            .data_out_num = I2S_DOUT,
            .data_in_num = I2S_PIN_NO_CHANGE
        };
        a2dp_sink.set_pin_config(my_pin_config);
        a2dp_sink.set_on_connection_state_changed(btConnectionCallback);
        
        setAudioEnabled(true);
        a2dp_sink.start(deviceName);
        Serial.println("[AUDIO] Bluetooth A2DP in ascolto!");
    } else {
        Serial.println("[AUDIO] Spegnimento Bluetooth A2DP e ripristino MP3 I2S...");
        a2dp_sink.end(); 
        
        // FIX: Forza lo sgancio del driver I2S che potrebbe essere rimasto appeso
        // dopo lo shutdown del modulo A2DP, evitando conflitti con AudioOutputI2S.
        if (i2s_driver_uninstall(I2S_NUM_0) == ESP_OK) {
            Serial.println("[AUDIO] Driver I2S sganciato con successo da A2DP.");
        }

        setAudioEnabled(false);
        appState.bluetoothConnected = false; 
        
        out = new AudioOutputI2S();
        out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
        Serial.println("[AUDIO] Driver I2S MP3 ripristinato.");
    }
}

void setVolume(uint8_t volume) {
    if (out) {
        // Limita per sicurezza il volume tra 1 e 10
        if (volume < 1) volume = 1;
        if (volume > 10) volume = 10;

        // --- MAPPATURA NON-LINEARE CON RANGE LIMITATO [0.1 - 0.9] ---
        // 1. Normalizza il volume (da 1-10) a un valore 't' tra 0.0 e 1.0
        float t = (volume - 1.0f) / 9.0f;
        
        // 2. Applica la curva esponenziale a 't'
        float curve = pow(t, 3.0f); 
        
        // 3. Mappa la curva sul range di guadagno finale: Min = 0.03, Max = 0.9
        float gain = 0.03f + (curve * 0.8f);

        Serial.printf("[AUDIO] Volume impostato a %d/10. Guadagno non-lineare applicato: %.4f\n", volume, gain);
        out->SetGain(gain);
    }
}

void setAudioEnabled(bool enable) {
    digitalWrite(I2S_SD_MODE, enable ? HIGH : LOW);
}

void stopAudio() {
    if (mp3 && mp3->isRunning()) {
        mp3->stop();
    }
    if (file) {
        file->close();
        file.reset();
    }
    
    // Spegne l'amplificatore per abbattere i consumi
    setAudioEnabled(false);
}

bool playMp3(const char* filename) {
    if (!audioOk) {
        Serial.printf("[AUDIO] ERRORE: Sistema audio non inizializzato correttamente. Impossibile riprodurre %s\n", filename);
        return false;
    }

    stopAudio(); // Ferma e pulisce se c'era già una riproduzione in corso

    file.reset(new AudioFileSourceLittleFS(filename));
    if (!file->isOpen()) {
        Serial.printf("[AUDIO] ERRORE: Impossibile aprire il file MP3: %s\n", filename);
        file.reset();
        return false;
    }

    Serial.printf("[AUDIO] File MP3 aperto: %s\n", filename);
    // Accende l'amplificatore
    setAudioEnabled(true);
    
    // Inizia la decodifica dell'MP3
    watchdog_feed();
    if (!mp3->begin(file.get(), out)) {
        Serial.printf("[AUDIO] ERRORE: impossibile avviare decodifica MP3 di %s\n", filename);
        stopAudio();
        return false;
    }
    
    Serial.printf("[AUDIO] Riproduzione iniziata: %s\n", filename);
    return true;
}

bool loopAudio() {
    if (mp3 && mp3->isRunning()) {
        watchdog_feed();
        if (!mp3->loop()) {
            // Se loop() ritorna false, la traccia è finita o c'è un errore
            stopAudio();
            return false;
        }
        return true;
    }
    return false;
}
