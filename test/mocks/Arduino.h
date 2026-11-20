// Arduino.h finto per i test su PC (pio test -e native).
// Copre solo ciò che usano i moduli compilati nei test; Serial scrive in un log.
#ifndef FAKE_ARDUINO_H
#define FAKE_ARDUINO_H

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <cmath>
#include <cstdlib>
#include <string>

using std::abs;

struct FakeSerial {
    std::string out;
    void print(const char* s) { out += s; }
    void println(const char* s = "") { out += s; out += "\n"; }
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        out += buf;
        return n;
    }
};
extern FakeSerial Serial;

// Orologio finto in microsecondi: delay() lo fa avanzare
extern int64_t fake_now_us;
inline int64_t esp_timer_get_time() { return fake_now_us; }
inline void delay(uint32_t ms) { fake_now_us += (int64_t)ms * 1000; }

#endif
