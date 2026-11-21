// Il font dell'orologio è stato ridotto ai soli caratteri stampati ("HH:MM").
// Ogni glifo rimasto deve essere identico all'originale: stesse metriche e
// stessa bitmap (CRC32 calcolati sul file prima della riduzione).
#include <unity.h>
#include <stdint.h>
#include <stddef.h>

#define PROGMEM
// Stesse struct di Adafruit_GFX/gfxfont.h
typedef struct { uint16_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; } GFXglyph;
typedef struct { uint8_t* bitmap; GFXglyph* glyph; uint16_t first, last; uint8_t yAdvance; } GFXfont;
#include "../../lib/fonts/Futura_Medium_onlynumbers112pt7b.h"

void setUp() {}
void tearDown() {}

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0u - (c & 1)));
    }
    return ~c;
}

struct Expected { uint8_t w, h, xAdv; int8_t xOff, yOff; uint32_t crc; };
static const Expected ORIGINAL['9' - '0' + 2] = {
    {115, 173, 127, 6, -127, 0xd65b42d1}, {49, 165, 127, 27, -127, 0xe66d6692},
    {107, 169, 127, 10, -127, 0x921ee152}, {103, 173, 127, 12, -127, 0xfdf8b02b},
    {120, 172, 127, 2, -127, 0xd0ec1b49}, {111, 169, 127, 7, -127, 0x5eecc439},
    {110, 173, 127, 9, -127, 0x9e982e52}, {120, 169, 127, 10, -127, 0x5c560c51},
    {103, 173, 127, 12, -127, 0x3736058f}, {110, 173, 127, 11, -127, 0x327e6a46},
    {25, 110, 63, 19, -105, 0xf219b53f}, // ':'
};

static void test_clock_glyphs_identical_to_original() {
    const GFXfont& f = Futura_Medium_onlynumbers112pt7b;
    TEST_ASSERT_EQUAL('0', f.first);
    TEST_ASSERT_EQUAL(':', f.last);
    TEST_ASSERT_EQUAL(255, f.yAdvance);
    for (int c = '0'; c <= ':'; c++) {
        const GFXglyph& g = f.glyph[c - f.first];
        const Expected& e = ORIGINAL[c - '0'];
        TEST_ASSERT_EQUAL(e.w, g.width);
        TEST_ASSERT_EQUAL(e.h, g.height);
        TEST_ASSERT_EQUAL(e.xAdv, g.xAdvance);
        TEST_ASSERT_EQUAL(e.xOff, g.xOffset);
        TEST_ASSERT_EQUAL(e.yOff, g.yOffset);
        TEST_ASSERT_EQUAL_HEX32(e.crc, crc32(f.bitmap + g.bitmapOffset, (g.width * g.height + 7) / 8));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_clock_glyphs_identical_to_original);
    return UNITY_END();
}
