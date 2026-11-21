/*
 * Canvas: a grid of styled cells, the virtual screen the UI draws into.
 *
 * Widgets draw into a canvas; the canvas is then turned into ANSI escape
 * sequences for the terminal, or into plain text for tests.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_CANVAS_H
#define TWINLINE_TUI_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

#include "tui/color.h"
#include "tui/strbuf.h"

enum {
    TW_STYLE_FG = 1 << 0,
    TW_STYLE_BG = 1 << 1,
    TW_STYLE_BOLD = 1 << 2,
    TW_STYLE_DIM = 1 << 3,
    TW_STYLE_ITALIC = 1 << 4,
    TW_STYLE_UNDERLINE = 1 << 5,
};

typedef struct tw_style {
    tw_rgb fg;
    tw_rgb bg;
    uint8_t flags; /* TW_STYLE_* */
} tw_style;

typedef struct tw_cell {
    char glyph[5]; /* one UTF-8 encoded character */
    uint8_t width; /* 1 or 2 columns; 0 for the right half of a wide character */
    tw_style style;
} tw_cell;

typedef struct tw_canvas {
    int width;
    int height;
    tw_cell *cells;
} tw_canvas;

/* Default terminal colours, no attributes. */
tw_style tw_style_plain(void);
/* Foreground colour, optionally with extra TW_STYLE_* flags. */
tw_style tw_style_fg(tw_rgb fg, uint8_t flags);

/* Creates a blank canvas. Returns false when out of memory. */
bool tw_canvas_init(tw_canvas *canvas, int width, int height);
void tw_canvas_free(tw_canvas *canvas);

/*
 * Writes UTF-8 `text` at (x, y), clipped to the canvas. Wide characters take
 * two columns; a wide character that does not fit is dropped. Tabs become a
 * space and other control characters are skipped. Returns the column after
 * the last character written.
 */
int tw_canvas_put(tw_canvas *canvas, int x, int y, const char *text, tw_style style);

/* Sets the background colour of `count` cells starting at (x, y). */
void tw_canvas_paint_bg(tw_canvas *canvas, int x, int y, int count, tw_rgb bg);

/* Appends row `y` as plain text, trailing spaces removed. */
void tw_canvas_row_text(const tw_canvas *canvas, int y, tw_strbuf *out);

/* Appends row `y` with ANSI styling for `mode`, trailing blanks removed. */
void tw_canvas_render_row(const tw_canvas *canvas, int y, tw_color_mode mode, tw_strbuf *out);

/* Appends the opening SGR sequence for `style` (nothing for plain/none). */
void tw_style_render(tw_style style, tw_color_mode mode, tw_strbuf *out);

#endif /* TWINLINE_TUI_CANVAS_H */
