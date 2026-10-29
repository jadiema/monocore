// made by sdk/tools/font_fr.py: do not edit (glyphs 96-127 of sdk/font.png)
// UTF-8 text -> glyph of the SDK font: font_next(&p) returns the glyph number + 32 (so 32-127
// are the ASCII characters themselves, 128-159 the French letters below), 10 for a new line,
// 0 at the end of the text, '?' for a character the font does not have (or broken UTF-8).
#pragma once
#include <stdint.h>

static const uint16_t font_fr_codes[32] = {0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00E0, 0x00E2, 0x00E7, 0x00EE, 0x00EF, 0x00F4, 0x00F9, 0x00FB, 0x00FC, 0x0153, 0x00C9, 0x00C8, 0x00CA, 0x00CB, 0x00C0, 0x00C2, 0x00C7, 0x00CE, 0x00CF, 0x00D4, 0x00D9, 0x00DB, 0x0152, 0x00AB, 0x00BB, 0x2026, 0x00B0, 0x20AC};   // é è ê ë à â ç î ï ô ù û ü œ É È Ê Ë À Â Ç Î Ï Ô Ù Û Œ « » … ° €
static const char font_fr_latin1[65] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

static inline int font_glyph(uint32_t u) {           // Unicode code -> glyph + 32
  if (u >= 32 && u < 127) return (int)u;
  for (int i = 0; i < 32; i++) if (font_fr_codes[i] == u) return 128 + i;
  if (u == 0x00A0) return ' ';
  if (u == 0x00D7) return 'x';
  if (u == 0x2009) return ' ';
  if (u == 0x2013) return '-';
  if (u == 0x2014) return '-';
  if (u == 0x2018) return '\'';
  if (u == 0x2019) return '\'';
  if (u == 0x201C) return '"';
  if (u == 0x201D) return '"';
  if (u == 0x202F) return ' ';
  if (u == 0x2212) return '-';
  if (u >= 0xC0 && u <= 0xFF) return font_fr_latin1[u - 0xC0];
  return '?';
}

static inline int font_next(const char **pp) {      // next character of UTF-8 text
  const uint8_t *p = (const uint8_t *)*pp;
  uint32_t u = *p;
  if (!u) return 0;
  p++;
  if (u >= 0x80) {
    int n = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;
    if (!n || (u & 0xC0) == 0x80) u = 0;              // stray continuation byte
    else {
      u &= 0x3Fu >> n;
      for (; n && (*p & 0xC0) == 0x80; n--) u = u << 6 | (*p++ & 0x3Fu);
      if (n) u = 0;                                   // sequence cut short
    }
    while ((*p & 0xC0) == 0x80) p++;
  }
  *pp = (const char *)p;
  return u == '\n' ? '\n' : u ? font_glyph(u) : '?';
}
