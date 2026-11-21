/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/color.h"

#include <stdlib.h>
#include <string.h>

tw_rgb tw_rgb_hex(uint32_t hex)
{
    tw_rgb c = {(uint8_t)((hex >> 16) & 0xFF), (uint8_t)((hex >> 8) & 0xFF), (uint8_t)(hex & 0xFF)};
    return c;
}

static uint8_t mix_channel(uint8_t a, uint8_t b, double t)
{
    double v = (double)a + ((double)b - (double)a) * t;
    if (v < 0.0)
        v = 0.0;
    if (v > 255.0)
        v = 255.0;
    return (uint8_t)(v + 0.5);
}

tw_rgb tw_rgb_mix(tw_rgb a, tw_rgb b, double t)
{
    tw_rgb c = {mix_channel(a.r, b.r, t), mix_channel(a.g, b.g, t), mix_channel(a.b, b.b, t)};
    return c;
}

/* The six levels of the xterm colour cube. */
static const int CUBE_LEVELS[6] = {0, 95, 135, 175, 215, 255};

static int nearest_cube_index(int value)
{
    int best = 0;
    for (int i = 1; i < 6; i++) {
        if (abs(CUBE_LEVELS[i] - value) < abs(CUBE_LEVELS[best] - value))
            best = i;
    }
    return best;
}

static int distance2(int r1, int g1, int b1, int r2, int g2, int b2)
{
    return (r1 - r2) * (r1 - r2) + (g1 - g2) * (g1 - g2) + (b1 - b2) * (b1 - b2);
}

uint8_t tw_rgb_to_256(tw_rgb color)
{
    int r = color.r;
    int g = color.g;
    int b = color.b;

    int ri = nearest_cube_index(r);
    int gi = nearest_cube_index(g);
    int bi = nearest_cube_index(b);
    int cube_dist = distance2(r, g, b, CUBE_LEVELS[ri], CUBE_LEVELS[gi], CUBE_LEVELS[bi]);

    /* Grey ramp: 24 shades from 8 to 238 in steps of 10 (indices 232-255). */
    int grey_index = ((r + g + b) / 3 - 8 + 5) / 10;
    if (grey_index < 0)
        grey_index = 0;
    if (grey_index > 23)
        grey_index = 23;
    int grey = 8 + grey_index * 10;
    int grey_dist = distance2(r, g, b, grey, grey, grey);

    if (grey_dist < cube_dist)
        return (uint8_t)(232 + grey_index);
    return (uint8_t)(16 + 36 * ri + 6 * gi + bi);
}

tw_color_mode tw_color_mode_from(const char *no_color, const char *colorterm, const char *term)
{
    if (no_color != NULL && no_color[0] != '\0')
        return TW_COLOR_NONE;
    if (colorterm != NULL &&
        (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0))
        return TW_COLOR_TRUE;
    if (term != NULL && strcmp(term, "dumb") == 0)
        return TW_COLOR_NONE;
    return TW_COLOR_256;
}

tw_color_mode tw_color_mode_detect(void)
{
    /* Called once at start-up, before any thread exists. */
    return tw_color_mode_from(getenv("NO_COLOR"),  // NOLINT(concurrency-mt-unsafe)
                              getenv("COLORTERM"), // NOLINT(concurrency-mt-unsafe)
                              getenv("TERM"));     // NOLINT(concurrency-mt-unsafe)
}
