# Font Personalizzati per UI

Questa cartella contiene i file dei font personalizzati per l'interfaccia utente.

## Come aggiungere un nuovo font:

1. Usa lo strumento online di Adafruit per convertire un font TTF/OTF in formato GFXfont:
   - Vai su: https://adafruit.github.io/Adafruit-GFX-Library/html/classAdafruit__GFX.html
   - O usa il tool: https://rop.nl/truetype2gfx/ (consigliato per ESP32)

2. Seleziona il font, dimensione, caratteri da includere (almeno ASCII base: 32-126).

3. Genera il file .h e salvalo qui come `NomeFont.h` (es. `Arial12pt.h`).

4. Il file deve contenere:
   - Un array di bitmaps: `const uint8_t NomeFontBitmaps[] PROGMEM = {...};`
   - Un array di glifi: `const GFXglyph NomeFontGlyphs[] PROGMEM = {...};`
   - La struttura font: `const GFXfont NomeFont PROGMEM = {...};`

5. Includi il font in `ui.cpp` aggiungendo `#include "fonts/NomeFont.h"`

6. Aggiungi il puntatore all'array `fonts` in `ui.cpp` (vedi codice).

Nota: I font occupano memoria FLASH, assicurati che non superino i limiti dell'ESP32.