/*
 * The interactive terminal UI.
 *
 * Output scrolls normally in the terminal; only the live region at the
 * bottom (activity line, input box, menu, status bar) is redrawn in place.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_UI_H
#define TWINLINE_TUI_UI_H

#include <stdbool.h>

#include "cmd/app.h"
#include "tui/color.h"
#include "tui/theme.h"

typedef struct tw_ui_options {
    const tw_theme *theme;
    tw_color_mode color;
    const char *open_file; /* opened at start, may be NULL */
} tw_ui_options;

/* True when standard input and output are both terminals. */
bool tw_ui_available(void);

/* Runs the UI until the user quits. Returns false if the terminal could not
 * be set up (the caller then falls back to the plain REPL). */
bool tw_ui_run(tw_app *app, const tw_ui_options *options);

#endif /* TWINLINE_TUI_UI_H */
