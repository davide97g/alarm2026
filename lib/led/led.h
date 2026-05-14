#ifndef LED_H
#define LED_H

#include <Arduino.h>

// Inizializza il canale PWM sul pin specificato
void led_init(uint8_t pin);

// Accende o imposta il LED alla luminosità desiderata (da 1 a 10)
void led_on(uint8_t brightness_1_to_10, bool fade = false);

// Imposta direttamente il valore PWM (0-255) per animazioni fluide
void led_set_duty(uint8_t duty);

// Spegne il LED, con l'opzione di eseguire un fade out morbido
void led_off(bool fade);

#endif // LED_H