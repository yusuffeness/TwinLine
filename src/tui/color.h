/*
 * Colours and terminal colour support.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_COLOR_H
#define TWINLINE_TUI_COLOR_H

#include <stdint.h>

typedef struct tw_rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} tw_rgb;

typedef enum tw_color_mode {
    TW_COLOR_NONE = 0, /* no colour escapes at all (NO_COLOR, dumb terminals) */
    TW_COLOR_256,      /* xterm 256-colour palette */
    TW_COLOR_TRUE,     /* 24-bit colour */
} tw_color_mode;

/* 0xRRGGBB to tw_rgb. */
tw_rgb tw_rgb_hex(uint32_t hex);

/* Linear interpolation between two colours, t in [0, 1]. */
tw_rgb tw_rgb_mix(tw_rgb a, tw_rgb b, double t);

/* Nearest entry of the xterm 256-colour palette (cube or grey ramp). */
uint8_t tw_rgb_to_256(tw_rgb color);

/*
 * Chooses the colour mode from environment values (any may be NULL):
 * NO_COLOR set and non-empty disables colour; COLORTERM "truecolor" or
 * "24bit" enables 24-bit colour; TERM "dumb" disables colour; anything else
 * gets the 256-colour palette.
 */
tw_color_mode tw_color_mode_from(const char *no_color, const char *colorterm, const char *term);

/* tw_color_mode_from() applied to the process environment. */
tw_color_mode tw_color_mode_detect(void);

#endif /* TWINLINE_TUI_COLOR_H */
