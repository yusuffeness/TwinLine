/*
 * Colour themes for the terminal UI.
 *
 * Body text always uses the terminal's own foreground colour, so every theme
 * stays readable on both light and dark terminal backgrounds; themes only
 * choose the accents.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_THEME_H
#define TWINLINE_TUI_THEME_H

#include <stddef.h>

#include "tui/color.h"

typedef struct tw_theme {
    const char *name;
    tw_rgb accent;    /* brand colour: logo start, prompt, focused borders */
    tw_rgb accent2;   /* secondary accent: logo end, progress */
    tw_rgb success;   /* check marks, inserted words */
    tw_rgb warning;   /* unsaved-changes marker */
    tw_rgb error;     /* error marks, deleted words */
    tw_rgb muted;     /* hints, status bar, secondary text */
    tw_rgb border;    /* unfocused borders and rules */
    tw_rgb selection; /* background of the selected menu item */
} tw_theme;

/* The theme used when none is chosen ("dark"). */
const tw_theme *tw_theme_default(void);

/* Looks a theme up by name; NULL when unknown. */
const tw_theme *tw_theme_find(const char *name);

size_t tw_theme_count(void);
const tw_theme *tw_theme_at(size_t index);

#endif /* TWINLINE_TUI_THEME_H */
