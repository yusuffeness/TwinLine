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
#include "twinline/analysis.h"

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

/* Formats 55312000 as "55.3M" (K, M, B suffixes; plain below 1000). */
void tw_format_compact(unsigned long long value, char *buf, size_t size);

/* ------------------------------------------------------------- animation */

/* Cubic ease-out of t, clamped to [0, 1]. */
double tw_ease_out(double t);

/* Spinner glyph for animation time `t` in seconds. */
const char *tw_spinner_frame(double t);

/*
 * Progress bar of `width` cells: a colour gradient from `from` to `to`, with
 * the last filled cell drawn to an eighth of a cell, over a `track` of light
 * shade.
 */
void tw_draw_progress(tw_canvas *canvas, int x, int y, int width, double fraction, tw_rgb from,
                      tw_rgb to, tw_rgb track);

/* Words of `a` and `b` (split on spaces) that the other line lacks, compared
 * case-insensitively and ignoring punctuation: bit i set = word i changed.
 * Lines longer than 64 words compare their first 64 words. */
void tw_word_diff(const char *a, const char *b, uint64_t *changed_a, uint64_t *changed_b);

/* ---------------------------------------------------------------- welcome */

typedef struct tw_welcome {
    const tw_theme *theme;
    int width;           /* terminal width */
    const char *version; /* "0.6.0" */
    const char *cwd;     /* shown shortened when long */
    double shine;        /* column of the light sweeping over the logo; < 0 for none */
} tw_welcome;

/* Draws the welcome banner into a new canvas sized to fit it. */
bool tw_welcome_render(const tw_welcome *welcome, tw_canvas *out);

/* ----------------------------------------------------------- live region */

typedef struct tw_menu_item {
    const char *label;
    const char *detail;  /* may be NULL */
    uint64_t match_bits; /* bit i set: byte i of `label` matched the filter */
} tw_menu_item;

/* A running analysis, shown above the input box. */
typedef struct tw_activity {
    const char *label;       /* "Analyzing" */
    double elapsed;          /* seconds */
    double fraction;         /* 0..1 */
    unsigned long long done; /* pairs */
    unsigned long long total;
    size_t threads;
    bool cancellable; /* show "esc to cancel" */
    bool cancelling;
    const double *thread_fractions; /* per-thread panel, may be NULL */
    size_t thread_count;
} tw_activity;

typedef enum tw_toast_kind {
    TW_TOAST_SUCCESS,
    TW_TOAST_WARNING,
    TW_TOAST_INFO,
} tw_toast_kind;

#define TW_TOAST_SECONDS 2.6 /* how long a toast stays on screen */

/* Fields are grouped by meaning; there is one per screen, so padding is cheap. */
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
typedef struct tw_view {
    const tw_theme *theme;
    int width;                   /* terminal width */
    const char *input;           /* text in the input box */
    size_t cursor;               /* cursor position, as a byte offset into `input` */
    const char *title;           /* label in the top border (e.g. reverse search), may be NULL */
    const char *activity;        /* plain line above the box, may be NULL */
    const tw_activity *progress; /* animated analysis progress, may be NULL */
    bool busy;                   /* draw the box border in the secondary accent */

    double time;  /* animation clock in seconds */
    bool animate; /* false: no spinner, shimmer or pulse (reduced motion) */

    const char *toast; /* short notice at the top right, may be NULL */
    tw_toast_kind toast_kind;
    double toast_age; /* seconds since the toast appeared */

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

/* ------------------------------------------------------------ result card */

typedef struct tw_result_card {
    const tw_theme *theme;
    int width;
    const tw_analysis_result *result;
    double progress; /* 0..1: numbers count up from zero as it grows */
} tw_result_card;

/* Draws a finished analysis as a card into a new canvas. */
bool tw_card_render(const tw_result_card *card, tw_canvas *out);

#endif /* TWINLINE_TUI_WIDGETS_H */
