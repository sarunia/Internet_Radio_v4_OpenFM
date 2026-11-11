#pragma once
/*
  OpenFmPolishText.h - polskie znaki UTF-8 dla Adafruit GFX / GFXcanvas16.

  Wlasne bitmapy 5x10 zawieraja: a/c/e/l/n/o/s/z z polskimi znakami,
  rowniez ich odpowiedniki wielkie. Zwykle znaki ASCII nadal sa rysowane
  przez oryginalna czcionke Adafruit GFX (setFont(nullptr)).

  Uzycie: OpenFmPolishText::print(canvas, tekst, prawaGranica, kolorRGB565, 2);
  Nie korzysta z dodatkowych bibliotek ani z pamieci alokowanej dynamicznie.
  Zasieg: teksty jednoliniowe w polach o zadanej szerokosci.
*/
#include <Arduino.h>
#include <Adafruit_GFX.h>

namespace OpenFmPolishText {
struct Glyph {uint16_t unicode; uint8_t bitmap[7];}; // 5*10 = 50 bitow
static const Glyph GLYPHS[] PROGMEM = {
  {0x0104, {0x00, 0x1D, 0x18, 0xFE, 0x31, 0x89, 0x80}}, // Ą,
  {0x0105, {0x00, 0x00, 0xE0, 0xBE, 0x33, 0x68, 0xC0}}, // ą,
  {0x0106, {0x31, 0x1F, 0x08, 0x42, 0x10, 0x78, 0x00}}, // Ć,
  {0x0107, {0x31, 0x00, 0xE8, 0xC2, 0x11, 0x70, 0x00}}, // ć,
  {0x0118, {0x00, 0x3F, 0x08, 0x7A, 0x10, 0xF9, 0x80}}, // Ę,
  {0x0119, {0x00, 0x00, 0xE8, 0xFE, 0x11, 0x70, 0xC0}}, // ę,
  {0x0141, {0x00, 0x21, 0x0C, 0x72, 0x10, 0xF8, 0x00}}, // Ł,
  {0x0142, {0x00, 0x08, 0x46, 0x38, 0x84, 0x30, 0x00}}, // ł,
  {0x0143, {0x31, 0x23, 0x9A, 0xCE, 0x31, 0x88, 0x00}}, // Ń,
  {0x0144, {0x31, 0x01, 0xE8, 0xC6, 0x31, 0x88, 0x00}}, // ń,
  {0x00D3, {0x31, 0x1D, 0x18, 0xC6, 0x31, 0x70, 0x00}}, // Ó,
  {0x00F3, {0x31, 0x00, 0xE8, 0xC6, 0x31, 0x70, 0x00}}, // ó,
  {0x015A, {0x31, 0x1F, 0x08, 0x38, 0x21, 0xF0, 0x00}}, // Ś,
  {0x015B, {0x31, 0x00, 0xF8, 0x38, 0x21, 0xF0, 0x00}}, // ś,
  {0x0179, {0x31, 0x3E, 0x11, 0x11, 0x10, 0xF8, 0x00}}, // Ź,
  {0x017A, {0x31, 0x01, 0xF1, 0x11, 0x10, 0xF8, 0x00}}, // ź,
  {0x017B, {0x20, 0x3E, 0x11, 0x11, 0x10, 0xF8, 0x00}}, // Ż,
  {0x017C, {0x20, 0x01, 0xF1, 0x11, 0x10, 0xF8, 0x00}}, // ż
};

// Mapowanie ograniczone do polskich liter; inne Unicode pokazywane jako '?'.
static inline const Glyph* findGlyph(uint16_t cp) {
  for (size_t i=0; i<sizeof(GLYPHS)/sizeof(GLYPHS[0]); ++i) {
    if (pgm_read_word(&GLYPHS[i].unicode) == cp) return &GLYPHS[i];
  }
  return nullptr;
}

// Rysuje glif w aktualnej pozycji kursora, zachowujac 6 pikseli szerokosci
// znaku przy skali 1 (tak samo jak klasyczna czcionka 5x7 Adafruit GFX).
static inline void drawGlyph(GFXcanvas16& canvas, const Glyph* glyph,
                             uint16_t color, uint8_t scale) {
  const int x=canvas.getCursorX(), y=canvas.getCursorY();
  for (uint8_t row=0; row<10; ++row) {
    for (uint8_t col=0; col<5; ++col) {
      const uint8_t offset = row*5+col;
      const uint8_t b = pgm_read_byte(&glyph->bitmap[offset/8]);
      if (b & (0x80 >> (offset%8)))
        canvas.fillRect(x+col*scale, y+row*scale, scale, scale, color);
    }
  }
  canvas.setCursor(x+6*scale,y);
}

// Dekodowanie Unicode UTF-8 nie rozcina polskich znakow w polowie.
// limitX - prawa granica napisu w pikselach, NIE liczba bajtow String.
static inline void print(GFXcanvas16& canvas, const String& value,
                         int limitX, uint16_t color, uint8_t scale=2) {
  for (size_t i=0; i<value.length();) {
    if (canvas.getCursorX()+6*scale > limitX) break;
    const uint8_t a=(uint8_t)value[i++];
    if (a<0x80) {
      if(a>=32 && a<=126) canvas.write(a);
      else if(a=='\t') canvas.write(' ');
      continue;
    }
    uint16_t cp=0;
    if((a&0xE0)==0xC0 && i<value.length()) {
      const uint8_t b=(uint8_t)value[i];
      if((b&0xC0)==0x80){++i; cp=((a&0x1F)<<6)|(b&0x3F);}
    } else if((a&0xF0)==0xE0 && i+1<value.length()) {
      const uint8_t b=(uint8_t)value[i], c=(uint8_t)value[i+1];
      if((b&0xC0)==0x80 && (c&0xC0)==0x80) {
        i+=2; cp=((a&0x0F)<<12)|((b&0x3F)<<6)|(c&0x3F);
      }
    } else if((a&0xF8)==0xF0 && i+2<value.length()) {
      // Pomijamy pozostale bajty nieobslugiwanych 4-bajtowych kodow UTF-8.
      uint8_t skipped=0;
      while(skipped<3 && i<value.length() && (((uint8_t)value[i])&0xC0)==0x80) {
        ++i; ++skipped;
      }
    }
    // Typowe myslniki Unicode -> zwykly znak '-' kompatybilny z GFX.
    if(cp==0x2013 || cp==0x2014 || cp==0x2212) {canvas.write('-');continue;}
    const Glyph* glyph=findGlyph(cp);
    if(glyph) drawGlyph(canvas,glyph,color,scale);
    else canvas.write('?');
  }
}
}
