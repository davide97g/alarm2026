#include "led.h"
#include "../watchdog/watchdog.h"
#include "../logic/logic.h"

static uint8_t led_pin = 0;
static const uint8_t led_channel = 0; // Usiamo il canale PWM 0 dell'ESP32

void led_init(uint8_t pin) {
    led_pin = pin;
    
    // Configura PWM: Canale 0, 5000 Hz, Risoluzione 8-bit (0-255)
    ledcSetup(led_channel, 5000, 8);
    ledcAttachPin(led_pin, led_channel);
    ledcWrite(led_channel, 0); // Assicurati che parta spento
}

void led_on(uint8_t brightness_1_to_10, bool fade) {
    if (brightness_1_to_10 > 10) brightness_1_to_10 = 10;
    if (brightness_1_to_10 < 1) brightness_1_to_10 = 1;
    
    // Curva quadratica per l'occhio umano (lib/logic)
    uint32_t target_duty = ledCurveDuty(brightness_1_to_10);
    
    if (fade) {
        uint32_t current_duty = ledcRead(led_channel);
        if (current_duty < target_duty) { // Fade IN (o aumento luminosità)
            for (int i = current_duty; i <= target_duty; i += 1) {
                watchdog_feed(); // Previeni blocco del watchdog
                ledcWrite(led_channel, i);
                delay(6); // Ritardo per rendere il fade visibile
            }
        } else if (current_duty > target_duty) { // Fade OUT parziale (riduzione luminosità)
            for (int i = current_duty; i >= target_duty; i -= 1) {
                watchdog_feed();
                ledcWrite(led_channel, i);
                delay(3);
            }
        }
    }
    
    // Forza il valore esatto finale per sicurezza
    ledcWrite(led_channel, target_duty);
}

void led_set_duty(uint8_t duty) {
    ledcWrite(led_channel, duty);
}

void led_off(bool fade) {
    if (fade) {
        // Legge l'attuale duty cycle e lo riduce gradualmente
        uint32_t current_duty = ledcRead(led_channel);
        for (int i = current_duty; i >= 0; i -= 1) {
            watchdog_feed(); // Previeni blocco del watchdog durante il fade out
            ledcWrite(led_channel, i);
            delay(8); // Ritardo per rendere il fade visibile
        }
    }

    // Forza lo spegnimento completo
    ledcWrite(led_channel, 0);
}