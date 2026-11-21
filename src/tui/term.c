/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/term.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Leave bracketed paste, reset attributes, show the cursor. */
#define RESET_SEQUENCE "\x1b[?2004l\x1b[0m\x1b[?25h"

static struct termios original;
static volatile sig_atomic_t raw_active = 0;
static volatile sig_atomic_t resized = 0;
static bool handlers_installed = false;

static void restore_now(void)
{
    if (!raw_active)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    ssize_t ignored = write(STDOUT_FILENO, RESET_SEQUENCE, sizeof RESET_SEQUENCE - 1);
    (void)ignored;
    raw_active = 0;
}

/* Only async-signal-safe calls in here: tcsetattr, write, signal, raise. */
static void on_fatal_signal(int sig)
{
    restore_now();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void on_resize(int sig)
{
    (void)sig;
    resized = 1;
}

static void install_handlers(void)
{
    if (handlers_installed)
        return;
    handlers_installed = true;
    atexit(restore_now);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_fatal_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    /* No SA_RESTART: a resize interrupts poll() so the UI redraws at once. */
    sa.sa_handler = on_resize;
    sigaction(SIGWINCH, &sa, NULL);
}

bool tw_term_enable_raw(void)
{
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &original) != 0)
        return false;
    install_handlers();

    struct termios raw = original;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= (tcflag_t)CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    /* Output post-processing stays on, so "\n" still moves to column 0. */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
        return false;
    raw_active = 1;

    static const char enable[] = "\x1b[?2004h"; /* bracketed paste */
    tw_term_write(enable, sizeof enable - 1);
    return true;
}

void tw_term_restore(void)
{
    restore_now();
}

bool tw_term_size(int *columns, int *rows)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0)
        return false;
    *columns = ws.ws_col;
    *rows = ws.ws_row;
    return true;
}

bool tw_term_take_resize(void)
{
    if (!resized)
        return false;
    resized = 0;
    return true;
}

void tw_term_write(const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = write(STDOUT_FILENO, data, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        data += n;
        len -= (size_t)n;
    }
}

int tw_term_wait_input(int timeout_ms)
{
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0)
        return -1;
    return rc > 0 ? 1 : 0;
}
