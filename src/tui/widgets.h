/*
 * Drawing primitives and the two composite views of the terminal UI:
 * the welcome banner (printed once into the scrollback) and the live region
 * (input box, command menu and status bar, redrawn in place).
 *
 * Everything here is a pure function of its inputs and draws into a canvas,
 * so it can be tested without a terminal.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_WIDGETS_H
#define TWINLINE_TUI_WIDGETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tui/canvas.h"
#include "tui/theme.h"

/* Rounded box: ╭─╮ │ │ ╰─╯. */
void tw_draw_box(tw_canvas *canvas, int x, int y, int width, int height, tw_style border);

/* Text clipped to `max_width` columns, ending in "…" when cut. Returns the
 * column after the text. */
int tw_draw_text(tw_canvas *canvas, int x, int y, int max_width, const char *text, tw_style style);

/* Text whose colour runs from `from` at column `x0` to `to` at column
 * `x0 + span - 1`; characters outside the span get the nearest end colour. */
void tw_draw_gradient(tw_canvas *canvas, int x, int y, const char *text, tw_rgb from, tw_rgb to,
                      int x0, int span, uint8_t flags);

/* Formats 16357 as "16,357". */
void tw_format_count(size_t value, char *buf, size_t size);

/* ---------------------------------------------------------------- welcome */

typedef struct tw_welcome {
    const tw_theme *theme;
    int width;           /* terminal width */
    const char *version; /* "0.5.0" */
    const char *cwd;     /* shown shortened when long */
} tw_welcome;

/* Draws the welcome banner into a new canvas sized to fit it. */
bool tw_welcome_render(const tw_welcome *welcome, tw_canvas *out);

/* ----------------------------------------------------------- live region */

typedef struct tw_menu_item {
    const char *label;
    const char *detail;  /* may be NULL */
    uint64_t match_bits; /* bit i set: byte i of `label` matched the filter */
} tw_menu_item;

typedef struct tw_view {
    const tw_theme *theme;
    int width;            /* terminal width */
    const char *input;    /* text in the input box */
    size_t cursor;        /* cursor position, as a byte offset into `input` */
    const char *title;    /* label in the top border (e.g. reverse search), may be NULL */
    const char *activity; /* line above the box (e.g. analysis progress), may be NULL */
    bool busy;            /* draw the box border in the secondary accent */

    const tw_menu_item *items; /* command or file menu under the box */
    size_t item_count;
    size_t selected;

    const char *hint; /* muted line under the box/menu, may be NULL */

    /* Status bar. */
    const char *file; /* may be NULL */
    bool modified;
    size_t lines;
    size_t threads;
    bool exhaustive;
    bool turkish;
} tw_view;

#define TW_MENU_VISIBLE 8 /* menu rows shown at once */

/*
 * Draws the live region into a new canvas of the needed height and reports
 * where the terminal cursor belongs. Returns false when out of memory.
 */
bool tw_view_render(const tw_view *view, tw_canvas *out, int *cursor_x, int *cursor_y);

#endif /* TWINLINE_TUI_WIDGETS_H */
