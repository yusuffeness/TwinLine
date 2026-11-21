/*
 * Low-level terminal control: raw mode, size, and safe restoration.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_TERM_H
#define TWINLINE_TUI_TERM_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Puts the terminal in raw mode (no echo, no line buffering, no signal
 * keys) and enables bracketed paste. The original state is restored by
 * tw_term_restore(), at exit, and when the process receives SIGTERM, SIGHUP
 * or SIGINT, so the user's shell is never left broken.
 */
bool tw_term_enable_raw(void);
void tw_term_restore(void);

/* Current terminal size. Returns false if it cannot be determined. */
bool tw_term_size(int *columns, int *rows);

/* True (once) after the terminal was resized. */
bool tw_term_take_resize(void);

/* Writes everything, retrying on partial writes and EINTR. */
void tw_term_write(const char *data, size_t len);

/* Waits up to `timeout_ms` for input on standard input. Returns 1 when input
 * is ready, 0 on timeout, -1 when interrupted (for example by a resize). */
int tw_term_wait_input(int timeout_ms);

#endif /* TWINLINE_TUI_TERM_H */
