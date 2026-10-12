#ifndef SPATIAL_FONT_H
#define SPATIAL_FONT_H

#include <stdint.h>

#define FONT_GLYPH_WIDTH 8
#define FONT_GLYPH_HEIGHT 13
#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR 126

/*
 * 8x13 Monochrome bitmap font for printable ASCII characters (32 to 126).
 * Each character is represented by 13 bytes, each byte corresponding to a row (MSB = leftmost pixel).
 */
extern const uint8_t g_font_8x13[(FONT_LAST_CHAR - FONT_FIRST_CHAR + 1)][FONT_GLYPH_HEIGHT];

static inline const uint8_t *font_get_glyph(char c) {
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) {
        c = '?';
    }
    return g_font_8x13[(unsigned char)c - FONT_FIRST_CHAR];
}

#endif /* SPATIAL_FONT_H */
