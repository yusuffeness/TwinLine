/*
 * SPDX-License-Identifier: MIT
 *
 * Threading: the main thread reads keys and runs commands; the analysis
 * worker reports results through tw_app.emit. Both write to the terminal
 * only while holding ui->lock, and the main thread holds it whenever it
 * changes the UI state, so a background result never tears the input line.
 * The lock is released while a command runs. Lock order: ui->lock, then the
 * session lock.
 */
#include "tui/ui.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cmd/commands.h"
#include "tui/complete.h"
#include "tui/keys.h"
#include "tui/lineedit.h"
#include "tui/strbuf.h"
#include "tui/term.h"
#include "tui/widgets.h"
#include "twinline/utf8.h"
#include "twinline/version.h"

#define MAX_ITEMS     64
#define LABEL_SIZE    32
#define CTRL_C_WINDOW 2.0
#define HIDE_CURSOR   "\x1b[?25l"
#define SHOW_CURSOR   "\x1b[?25h"
#define RESET         "\x1b[0m"

typedef enum menu_kind {
    MENU_NONE,
    MENU_COMMANDS,
    MENU_FILES,
} menu_kind;

#define INTRO_SECONDS      1.6        /* light sweeping over the logo at start */
#define CARD_SECONDS       0.9        /* numbers of a result card counting up */
#define FRAME_SECONDS      (1.0 / 30) /* animation frame rate */
#define SLOW_FRAME_SECONDS 0.25       /* progress refresh rate without animation */

/* Fields are grouped by meaning; there is one per screen, so padding is cheap. */
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
typedef struct tw_ui {
    tw_app *app;
    const tw_theme *theme;
    tw_color_mode color;
    pthread_mutex_t lock;
    int columns;

    tw_line line;
    tw_history history;
    size_t history_pos; /* history.count when not browsing */
    char *draft;        /* the line being typed before browsing started */

    bool searching; /* Ctrl+R reverse search */
    tw_line query;
    size_t search_match;
    bool search_found;
    char *search_saved;
    char *prefill; /* text "edit" asked to put into the input line */

    menu_kind menu;
    bool menu_dismissed;
    tw_menu_item items[MAX_ITEMS];
    size_t command_of[MAX_ITEMS]; /* command table index of each item */
    char labels[MAX_ITEMS][LABEL_SIZE];
    size_t item_count;
    size_t selected;
    tw_completions files;

    char hint[200];
    double ctrl_c_time;
    bool in_paste;

    /* Terminal state of the live region. */
    bool live_drawn;
    int live_cursor_row;
    tw_canvas prev; /* what the live region shows now, for row-by-row updates */
    bool prev_valid;
    bool closing;
    bool quit;
    tw_strbuf out;

    /* Animation. */
    bool animate;
    double start_time;
    pthread_t render_thread;
    bool render_started;
    pthread_cond_t wake;
    bool stopping;
    char cwd[1024];
    bool intro_active; /* the welcome banner is still in the live region */
    double intro_start;
    char *pending_open; /* file to open once the intro is over */
    bool card_active;   /* a result card is counting up in the live region */
    double card_start;
    tw_analysis_result card;
    char toast[96];
    tw_toast_kind toast_kind;
    double toast_start;
    bool toast_active;
    bool show_threads; /* /threads: per-thread progress bars */
} tw_ui;

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------- output */

static void style(tw_ui *ui, tw_strbuf *sb, tw_rgb color, uint8_t flags)
{
    tw_style_render(tw_style_fg(color, flags), ui->color, sb);
}

static void plain_style(tw_ui *ui, tw_strbuf *sb, uint8_t flags)
{
    tw_style s = tw_style_plain();
    s.flags = flags;
    tw_style_render(s, ui->color, sb);
}

static void flush(tw_ui *ui)
{
    tw_term_write(ui->out.data ? ui->out.data : "", ui->out.len);
    tw_strbuf_clear(&ui->out);
}

static void forget_live(tw_ui *ui)
{
    ui->live_drawn = false;
    if (ui->prev_valid)
        tw_canvas_free(&ui->prev);
    ui->prev_valid = false;
}

static void erase_live(tw_ui *ui)
{
    if (!ui->live_drawn)
        return;
    tw_strbuf_append(&ui->out, "\r");
    if (ui->live_cursor_row > 0)
        tw_strbuf_appendf(&ui->out, "\x1b[%dA", ui->live_cursor_row);
    tw_strbuf_append(&ui->out, "\x1b[J");
    forget_live(ui);
}

/* Appends canvas rows as ANSI lines, each ending in a newline. */
static void append_canvas(tw_ui *ui, tw_strbuf *sb, const tw_canvas *canvas)
{
    for (int y = 0; y < canvas->height; y++) {
        tw_canvas_render_row(canvas, y, ui->color, sb);
        tw_strbuf_append(sb, "\n");
    }
}

static bool render_welcome(tw_ui *ui, double shine, tw_canvas *out)
{
    tw_welcome welcome = {ui->theme, ui->columns - 1, TWINLINE_VERSION, ui->cwd, shine};
    return tw_welcome_render(&welcome, out);
}

static bool render_card(tw_ui *ui, double progress, tw_canvas *out)
{
    tw_result_card card = {ui->theme, ui->columns - 1, &ui->card, progress};
    return tw_card_render(&card, out);
}

static void fill_activity(tw_analysis_progress *progress, const char *label, bool cancellable,
                          bool cancelling, double *fractions, size_t max_threads, tw_activity *a)
{
    memset(a, 0, sizeof *a);
    a->label = label;
    a->elapsed = tw_analysis_progress_elapsed(progress);
    a->fraction = tw_analysis_progress_fraction(progress);
    a->done = atomic_load_explicit(&progress->pairs_done, memory_order_relaxed);
    a->total = atomic_load_explicit(&progress->pairs_total, memory_order_relaxed);
    a->threads = atomic_load_explicit(&progress->threads, memory_order_relaxed);
    a->cancellable = cancellable;
    a->cancelling = cancelling;
    if (fractions != NULL) {
        a->thread_count = tw_analysis_progress_threads(progress, fractions, max_threads);
        a->thread_fractions = fractions;
    }
}

static void build_view(tw_ui *ui, double now, tw_view *view, tw_activity *activity,
                       double *fractions, char *title, size_t title_size, char *file,
                       size_t file_size)
{
    tw_app *app = ui->app;
    memset(view, 0, sizeof *view);
    view->theme = ui->theme;
    view->width = ui->columns - 1;
    view->input = ui->line.buf;
    view->cursor = ui->line.cursor;
    view->time = now - ui->start_time;
    view->animate = ui->animate;

    if (ui->searching) {
        snprintf(title, title_size, "%s: %s",
                 ui->search_found || ui->query.len == 0 ? "reverse search" : "no match for",
                 ui->query.buf);
        view->title = title;
    }

    double *panel = ui->show_threads ? fractions : NULL;
    if (tw_job_is_running(&app->job)) {
        fill_activity(&app->job.progress, "Analyzing", true, tw_job_is_cancelling(&app->job), panel,
                      TW_PROGRESS_SLOTS, activity);
        view->progress = activity;
        view->busy = true;
    } else if (atomic_load(&app->sync_running)) {
        fill_activity(&app->sync_progress, "Analyzing", false, false, panel, TW_PROGRESS_SLOTS,
                      activity);
        view->progress = activity;
        view->busy = true;
    }

    if (ui->toast_active) {
        view->toast = ui->toast;
        view->toast_kind = ui->toast_kind;
        view->toast_age = now - ui->toast_start;
    }

    if (ui->menu != MENU_NONE) {
        view->items = ui->items;
        view->item_count = ui->item_count;
        view->selected = ui->selected;
    }
    view->hint = ui->hint[0] ? ui->hint : NULL;

    pthread_mutex_lock(&app->session.lock);
    if (app->buffer_count > 1) {
        snprintf(file, file_size, "%s [%zu/%zu]",
                 app->session.path != NULL ? app->session.path : "(untitled)", app->current + 1,
                 app->buffer_count);
        view->file = file;
    } else if (app->session.path != NULL) {
        snprintf(file, file_size, "%s", app->session.path);
        view->file = file;
    }
    view->modified = app->session.dirty;
    view->lines = app->session.doc.count;
    pthread_mutex_unlock(&app->session.lock);

    view->threads = tw_analysis_thread_count(&app->options);
    view->exhaustive = app->options.exhaustive;
    view->mode = app->options.strategy != TW_STRATEGY_JACCARD
                     ? tw_strategy_name(app->options.strategy)
                     : NULL;
    view->turkish = app->options.fold == TW_FOLD_TURKISH;
}

/* Composes the whole live region: an optional header (the welcome banner
 * during the intro, or a result card while it counts up) above the view. */
static bool compose_live(tw_ui *ui, tw_canvas *out, int *cx, int *cy)
{
    double now = now_seconds();
    char title[200];
    char file[512];
    double fractions[TW_PROGRESS_SLOTS];
    tw_activity activity;
    tw_view view;
    build_view(ui, now, &view, &activity, fractions, title, sizeof title, file, sizeof file);

    tw_canvas body;
    if (!tw_view_render(&view, &body, cx, cy))
        return false;

    tw_canvas header;
    bool has_header = false;
    if (ui->intro_active) {
        double t = (now - ui->intro_start) / INTRO_SECONDS;
        has_header = render_welcome(ui, -12.0 + t * 90.0, &header);
    } else if (ui->card_active) {
        has_header = render_card(ui, (now - ui->card_start) / CARD_SECONDS, &header);
    }
    if (!has_header) {
        *out = body;
        return true;
    }

    int width = body.width > header.width ? body.width : header.width;
    if (!tw_canvas_init(out, width, header.height + body.height)) {
        tw_canvas_free(&header);
        *out = body;
        return true;
    }
    tw_canvas_blit(out, &header, 0, 0);
    tw_canvas_blit(out, &body, 0, header.height);
    *cy += header.height;
    tw_canvas_free(&header);
    tw_canvas_free(&body);
    return true;
}

/* Moves the terminal cursor between rows of the live region. */
static void move_rows(tw_ui *ui, int from, int to)
{
    if (to < from)
        tw_strbuf_appendf(&ui->out, "\x1b[%dA", from - to);
    else if (to > from)
        tw_strbuf_appendf(&ui->out, "\x1b[%dB", to - from);
}

static void place_cursor(tw_ui *ui, int row_now, int cx, int cy)
{
    move_rows(ui, row_now, cy);
    tw_strbuf_append(&ui->out, "\r");
    if (cx > 0)
        tw_strbuf_appendf(&ui->out, "\x1b[%dC", cx);
    ui->live_cursor_row = cy;
}

/* Draws the live region at the cursor (which must be where it starts). */
static void draw_live_full(tw_ui *ui, tw_canvas *canvas, int cx, int cy)
{
    for (int y = 0; y < canvas->height; y++) {
        tw_canvas_render_row(canvas, y, ui->color, &ui->out);
        if (y + 1 < canvas->height)
            tw_strbuf_append(&ui->out, "\n");
    }
    place_cursor(ui, canvas->height - 1, cx, cy);
    ui->live_drawn = true;
    if (ui->prev_valid)
        tw_canvas_free(&ui->prev);
    ui->prev = *canvas;
    ui->prev_valid = true;
}

static void draw_live(tw_ui *ui)
{
    if (ui->closing)
        return;
    tw_canvas canvas;
    int cx = 0;
    int cy = 0;
    if (!compose_live(ui, &canvas, &cx, &cy))
        return;
    draw_live_full(ui, &canvas, cx, cy);
}

/*
 * Redraws the live region in place. When its size is unchanged only the rows
 * that differ are rewritten, so a spinner frame costs one short row instead
 * of the whole region. Caller holds ui->lock.
 */
static void refresh(tw_ui *ui)
{
    if (ui->closing)
        return;
    tw_canvas next;
    int cx = 0;
    int cy = 0;
    if (!compose_live(ui, &next, &cx, &cy))
        return;

    tw_strbuf_append(&ui->out, HIDE_CURSOR);
    if (ui->live_drawn && ui->prev_valid && ui->prev.width == next.width &&
        ui->prev.height == next.height) {
        int row = ui->live_cursor_row;
        for (int y = 0; y < next.height; y++) {
            if (tw_canvas_row_equal(&ui->prev, &next, y))
                continue;
            move_rows(ui, row, y);
            row = y;
            tw_strbuf_append(&ui->out, "\r\x1b[2K");
            tw_canvas_render_row(&next, y, ui->color, &ui->out);
        }
        place_cursor(ui, row, cx, cy);
        tw_canvas_free(&ui->prev);
        ui->prev = next;
    } else {
        erase_live(ui);
        draw_live_full(ui, &next, cx, cy);
    }
    tw_strbuf_append(&ui->out, SHOW_CURSOR);
    flush(ui);
    pthread_cond_signal(&ui->wake); /* an animation may have started */
}

/* Moves a finished intro or card out of the live region into the scrollback. */
static void commit_header(tw_ui *ui, tw_strbuf *sb)
{
    tw_canvas canvas;
    if (ui->intro_active) {
        ui->intro_active = false;
        if (render_welcome(ui, -1.0, &canvas)) {
            append_canvas(ui, sb, &canvas);
            tw_canvas_free(&canvas);
        }
    }
    if (ui->card_active) {
        ui->card_active = false;
        tw_strbuf_append(sb, "\n");
        if (render_card(ui, 1.0, &canvas)) {
            /* The card's first row is padding inside the live region. */
            for (int y = 1; y < canvas.height; y++) {
                tw_canvas_render_row(&canvas, y, ui->color, sb);
                tw_strbuf_append(sb, "\n");
            }
            tw_canvas_free(&canvas);
        }
    }
}

/* Prints `text` (ANSI, ending in a newline) above the live region. */
static void print_above(tw_ui *ui, const char *text)
{
    tw_strbuf_append(&ui->out, HIDE_CURSOR);
    erase_live(ui);
    commit_header(ui, &ui->out); /* keep the scrollback in order */
    tw_strbuf_append(&ui->out, text);
    draw_live(ui);
    tw_strbuf_append(&ui->out, SHOW_CURSOR);
    flush(ui);
    pthread_cond_signal(&ui->wake);
}

static void show_toast(tw_ui *ui, tw_toast_kind kind, const char *text)
{
    snprintf(ui->toast, sizeof ui->toast, "%s", text);
    ui->toast_kind = kind;
    ui->toast_start = now_seconds();
    ui->toast_active = true;
}

/* ---------------------------------------------------------- formatting */

/* True for C0 and C1 control characters other than the tab. Printed raw,
 * they would reach the terminal as commands: a document must never be able
 * to move the cursor, change colours or set the window title. */
static bool is_control(uint32_t cp)
{
    return (cp < 0x20 && cp != '\t') || (cp >= 0x7F && cp < 0xA0);
}

/* Appends `len` bytes of `text`, drawing the TW_MARK_ON ... TW_MARK_OFF
 * spans (search matches, the goto line) as highlighted and control
 * characters in caret notation (^[ for ESC), as `cat -v` does. */
static void append_marked(tw_ui *ui, tw_strbuf *sb, const char *text, size_t len)
{
    tw_style mark = tw_style_fg(tw_rgb_hex(0x111118), TW_STYLE_BOLD | TW_STYLE_BG);
    mark.bg = ui->theme->accent2;
    bool marked = false;
    size_t start = 0;
    size_t i = 0;
    while (i < len) {
        char c = text[i];
        if (c == TW_MARK_ON[0] || c == TW_MARK_OFF[0]) {
            tw_strbuf_append_n(sb, text + start, i - start);
            marked = c == TW_MARK_ON[0];
            if (marked)
                tw_style_render(mark, ui->color, sb);
            else
                tw_strbuf_append(sb, RESET);
            start = ++i;
            continue;
        }
        uint32_t cp = 0;
        size_t n =
            (unsigned char)c < 0x80 ? 1 : tw_utf8_decode((const unsigned char *)text + i, &cp);
        if (n == 1)
            cp = (unsigned char)c;
        if (i + n > len)
            n = len - i;
        if (!is_control(cp)) {
            i += n;
            continue;
        }
        tw_strbuf_append_n(sb, text + start, i - start);
        char caret[16];
        if (cp < 0x20 || cp == 0x7F)
            snprintf(caret, sizeof caret, "^%c", cp == 0x7F ? '?' : (char)(cp + 0x40));
        else
            snprintf(caret, sizeof caret, "<%02X>", (unsigned)cp); /* C1 control */
        style(ui, sb, ui->theme->muted, 0);
        tw_strbuf_append(sb, caret);
        tw_strbuf_append(sb, RESET);
        if (marked)
            tw_style_render(mark, ui->color, sb);
        i += n;
        start = i;
    }
    tw_strbuf_append_n(sb, text + start, len - start);
}

/* Appends `text` line by line, each line prefixed with the tree glyph. */
static void format_lines(tw_ui *ui, tw_strbuf *sb, const char *text, bool error, bool single_ok)
{
    const char *p = text;
    bool first = true;
    while (*p != '\0') {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        const char *next = end ? end + 1 : p + len;
        /* Reports indent their rows by two spaces; the tree glyph replaces that. */
        if (len >= 2 && p[0] == ' ' && p[1] == ' ') {
            p += 2;
            len -= 2;
        }

        if (first) {
            tw_strbuf_append(sb, "  ");
            style(ui, sb, ui->theme->muted, 0);
            tw_strbuf_append(sb, "⎿ ");
            tw_strbuf_append(sb, RESET);
        } else {
            tw_strbuf_append(sb, "    ");
        }

        if (error && len >= 9 && strncmp(p, "warning: ", 9) == 0) {
            p += 9;
            len -= 9;
            style(ui, sb, ui->theme->warning, TW_STYLE_BOLD);
            tw_strbuf_append(sb, "▲ ");
            tw_strbuf_append(sb, RESET);
            style(ui, sb, ui->theme->warning, 0);
        } else if (error) {
            if (len >= 7 && strncmp(p, "error: ", 7) == 0) {
                p += 7;
                len -= 7;
            }
            style(ui, sb, ui->theme->error, TW_STYLE_BOLD);
            tw_strbuf_append(sb, "✖ ");
            tw_strbuf_append(sb, RESET);
            style(ui, sb, ui->theme->error, 0);
        } else if (first && single_ok) {
            style(ui, sb, ui->theme->success, TW_STYLE_BOLD);
            tw_strbuf_append(sb, "✔ ");
            tw_strbuf_append(sb, RESET);
        } else if (len > 0 && p[len - 1] == ':' && p[0] != ' ') {
            plain_style(ui, sb, TW_STYLE_BOLD); /* section title */
        }
        append_marked(ui, sb, p, len);
        tw_strbuf_append(sb, RESET "\n");
        first = false;
        p = next;
    }
}

static bool is_single_line(const char *text)
{
    const char *nl = strchr(text, '\n');
    return text[0] != '\0' && (nl == NULL || nl[1] == '\0');
}

static void format_echo(tw_ui *ui, tw_strbuf *sb, const char *typed)
{
    tw_strbuf_append(sb, "\n");
    style(ui, sb, ui->theme->accent, TW_STYLE_BOLD);
    tw_strbuf_append(sb, "> ");
    tw_strbuf_append(sb, RESET);
    tw_strbuf_append(sb, typed);
    tw_strbuf_append(sb, "\n");
}

static void print_result(tw_ui *ui, const char *out_text, const char *err_text)
{
    tw_strbuf sb;
    tw_strbuf_init(&sb);
    bool has_err = err_text[0] != '\0';
    if (out_text[0] != '\0')
        format_lines(ui, &sb, out_text, false, !has_err && is_single_line(out_text));
    if (has_err)
        format_lines(ui, &sb, err_text, true, false);
    if (sb.len > 0)
        print_above(ui, tw_strbuf_str(&sb));
    tw_strbuf_free(&sb);
}

static void print_shortcuts(tw_ui *ui)
{
    static const char *const ROWS[][2] = {
        {"/", "open the command menu"},
        {"Tab", "complete a command or file name"},
        {"↑ ↓", "browse history, or move in the menu"},
        {"Ctrl+R", "search history"},
        {"Esc", "close the menu, cancel the analysis"},
        {"Ctrl+A / Ctrl+E", "start / end of line"},
        {"Ctrl+← / Ctrl+→", "previous / next word"},
        {"Ctrl+U / Ctrl+K", "delete to start / end of line"},
        {"Ctrl+W", "delete the previous word"},
        {"Ctrl+L", "clear the screen"},
        {"Ctrl+C twice", "exit"},
    };
    tw_strbuf sb;
    tw_strbuf_init(&sb);
    tw_strbuf_append(&sb, "\n");
    plain_style(ui, &sb, TW_STYLE_BOLD);
    tw_strbuf_append(&sb, "Keyboard shortcuts" RESET "\n");
    for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
        tw_strbuf_append(&sb, "  ");
        style(ui, &sb, ui->theme->accent, TW_STYLE_BOLD);
        tw_strbuf_append(&sb, ROWS[i][0]);
        tw_strbuf_append(&sb, RESET);
        int pad = 18 - (int)tw_utf8_str_width(ROWS[i][0]);
        for (int k = 0; k < pad; k++)
            tw_strbuf_append(&sb, " ");
        style(ui, &sb, ui->theme->muted, 0);
        tw_strbuf_append(&sb, ROWS[i][1]);
        tw_strbuf_append(&sb, RESET "\n");
    }
    print_above(ui, tw_strbuf_str(&sb));
    tw_strbuf_free(&sb);
}

static void compute_cwd(tw_ui *ui)
{
    char cwd[1024];
    if (getcwd(cwd, sizeof cwd) == NULL)
        snprintf(cwd, sizeof cwd, ".");
    const char *home = getenv("HOME"); // NOLINT(concurrency-mt-unsafe): main thread only
    size_t home_len = home ? strlen(home) : 0;
    if (home_len > 1 && strncmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '/' || cwd[home_len] == '\0'))
        snprintf(ui->cwd, sizeof ui->cwd, "~%s", cwd + home_len);
    else
        snprintf(ui->cwd, sizeof ui->cwd, "%s", cwd);
}

/* Shows the welcome banner: animated in the live region at first, then
 * committed to the scrollback (at once when animations are off). */
static void start_intro(tw_ui *ui)
{
    compute_cwd(ui);
    ui->intro_active = true;
    ui->intro_start = now_seconds();
    if (ui->animate && ui->columns - 1 >= 67)
        refresh(ui);
    else
        print_above(ui, ""); /* commits the banner right away */
}

/* Runs on the analysis worker thread. */
static void ui_emit(tw_app *app, const char *text, bool is_error)
{
    tw_ui *ui = app->ui;
    pthread_mutex_lock(&ui->lock);
    tw_strbuf sb;
    tw_strbuf_init(&sb);
    tw_strbuf_append(&sb, "\n");
    style(ui, &sb, is_error ? ui->theme->error : ui->theme->accent2, TW_STYLE_BOLD);
    tw_strbuf_append(&sb, is_error ? "✖ " : "✶ ");
    tw_strbuf_append(&sb, RESET);
    plain_style(ui, &sb, TW_STYLE_BOLD);
    const char *nl = strchr(text, '\n');
    size_t title_len = nl ? (size_t)(nl - text) : strlen(text);
    tw_strbuf_append_n(&sb, text, title_len);
    tw_strbuf_append(&sb, RESET "\n");
    if (nl != NULL && nl[1] != '\0')
        format_lines(ui, &sb, nl + 1, false, false);
    if (strstr(text, "cancelled") != NULL)
        show_toast(ui, TW_TOAST_WARNING, "Analysis cancelled");
    else if (is_error)
        show_toast(ui, TW_TOAST_WARNING, "Analysis failed");
    print_above(ui, tw_strbuf_str(&sb));
    tw_strbuf_free(&sb);
    pthread_mutex_unlock(&ui->lock);
}

/* A finished analysis (from the worker, or from analyze-sync on the main
 * thread while ui->lock is released): shown as a card that counts up. */
static void ui_on_result(tw_app *app, const tw_analysis_result *result)
{
    tw_ui *ui = app->ui;
    pthread_mutex_lock(&ui->lock);
    if (ui->card_active)
        print_above(ui, ""); /* commit the previous card first */
    ui->card = *result;
    ui->card_active = true;
    ui->card_start = now_seconds();

    char message[96];
    snprintf(message, sizeof message, "Analysis finished in %.2f s", result->seconds);
    show_toast(ui, TW_TOAST_SUCCESS, message);

    if (ui->animate && !ui->stopping)
        refresh(ui);
    else
        print_above(ui, ""); /* commits the card right away */
    pthread_mutex_unlock(&ui->lock);
}

/* ---------------------------------------------------------------- menus */

static const tw_command_info *command_named(const char *name, size_t len)
{
    for (size_t i = 0; i < tw_command_count(); i++) {
        const tw_command_info *info = tw_command_at(i);
        if (strlen(info->name) == len && strncmp(info->name, name, len) == 0)
            return info;
    }
    return NULL;
}

static bool takes_argument(const tw_command_info *info)
{
    return strchr(info->usage, ' ') != NULL;
}

static void close_file_menu(tw_ui *ui)
{
    if (ui->menu == MENU_FILES)
        ui->menu = MENU_NONE;
    tw_completions_free(&ui->files);
}

static void update_command_menu(tw_ui *ui)
{
    const char *s = ui->line.buf;
    if (ui->menu == MENU_FILES)
        return;
    if (ui->menu_dismissed || s[0] != '/' || strpbrk(s, " \t") != NULL) {
        ui->menu = MENU_NONE;
        return;
    }

    int scores[MAX_ITEMS];
    size_t count = 0;
    for (size_t i = 0; i < tw_command_count() && count < MAX_ITEMS; i++) {
        const tw_command_info *info = tw_command_at(i);
        if (strcmp(info->name, "exit") == 0)
            continue; /* alias of quit */
        int score;
        uint64_t bits;
        if (!tw_fuzzy_match(s + 1, info->name, &score, &bits))
            continue;

        /* Insert by score; ties keep table order. */
        size_t at = count;
        while (at > 0 && scores[at - 1] < score) {
            scores[at] = scores[at - 1];
            ui->command_of[at] = ui->command_of[at - 1];
            ui->items[at].match_bits = ui->items[at - 1].match_bits;
            at--;
        }
        scores[at] = score;
        ui->command_of[at] = i;
        ui->items[at].match_bits = bits << 1; /* labels start with '/' */
        count++;
    }
    for (size_t k = 0; k < count; k++) {
        const tw_command_info *info = tw_command_at(ui->command_of[k]);
        snprintf(ui->labels[k], LABEL_SIZE, "/%s", info->name);
        ui->items[k].label = ui->labels[k];
        ui->items[k].detail = info->summary;
    }
    ui->item_count = count;
    if (ui->selected >= count)
        ui->selected = 0;
    ui->menu = count > 0 ? MENU_COMMANDS : MENU_NONE;
}

/* Call after every change to the text of the line. */
static void after_edit(tw_ui *ui)
{
    close_file_menu(ui);
    ui->menu_dismissed = false;
    ui->history_pos = ui->history.count;
    ui->hint[0] = '\0';
    ui->selected = 0;
    update_command_menu(ui);
}

/* Offset where the argument of "/cmd arg" starts, or 0 if there is none. */
static size_t argument_offset(const char *s, const tw_command_info **info)
{
    size_t i = (s[0] == '/') ? 1 : 0;
    size_t name_start = i;
    while (s[i] != '\0' && s[i] != ' ' && s[i] != '\t')
        i++;
    if (s[i] == '\0')
        return 0;
    size_t name_len = i - name_start;
    if (name_len > 0 && s[name_start + name_len - 1] == '!')
        name_len--;
    *info = command_named(s + name_start, name_len);
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    return i;
}

static void replace_argument(tw_ui *ui, size_t offset, const char *text)
{
    ui->line.cursor = ui->line.len;
    while (ui->line.len > offset)
        tw_line_backspace(&ui->line);
    tw_line_insert(&ui->line, text);
}

static void accept_command(tw_ui *ui, size_t item)
{
    const tw_command_info *info = tw_command_at(ui->command_of[item]);
    char text[LABEL_SIZE + 2];
    snprintf(text, sizeof text, "/%s%s", info->name, takes_argument(info) ? " " : "");
    tw_line_set(&ui->line, text);
    after_edit(ui);
}

static void accept_file(tw_ui *ui)
{
    const tw_command_info *info = NULL;
    size_t offset = argument_offset(ui->line.buf, &info);
    char *choice = strdup(ui->files.items[ui->selected]);
    close_file_menu(ui);
    if (choice != NULL) {
        replace_argument(ui, offset, choice);
        free(choice);
    }
    ui->menu_dismissed = false;
    ui->hint[0] = '\0';
}

static void complete_file(tw_ui *ui)
{
    const tw_command_info *info = NULL;
    size_t offset = argument_offset(ui->line.buf, &info);
    if (offset == 0 || info == NULL || !info->takes_file)
        return;

    char *prefix = strdup(ui->line.buf + offset);
    if (prefix == NULL)
        return;
    close_file_menu(ui);
    if (tw_complete_path(prefix, &ui->files) != TW_OK || ui->files.count == 0) {
        snprintf(ui->hint, sizeof ui->hint, "No file matches '%s'", prefix);
        free(prefix);
        return;
    }

    if (ui->files.count == 1) {
        replace_argument(ui, offset, ui->files.items[0]);
        tw_completions_free(&ui->files);
    } else {
        size_t common = tw_completions_common_prefix(&ui->files);
        if (common > strlen(prefix)) {
            char *shared = strndup(ui->files.items[0], common);
            if (shared != NULL) {
                replace_argument(ui, offset, shared);
                free(shared);
            }
        }
        size_t count = ui->files.count < MAX_ITEMS ? ui->files.count : MAX_ITEMS;
        for (size_t i = 0; i < count; i++) {
            const char *item = ui->files.items[i];
            const char *slash = strrchr(item, '/');
            /* show the last component; directories keep their trailing '/' */
            if (slash != NULL && slash[1] == '\0') {
                const char *prev = slash;
                while (prev > item && prev[-1] != '/')
                    prev--;
                ui->items[i].label = prev;
            } else {
                ui->items[i].label = slash ? slash + 1 : item;
            }
            ui->items[i].detail = NULL;
            ui->items[i].match_bits = 0;
        }
        ui->item_count = count;
        ui->selected = 0;
        ui->menu = MENU_FILES;
        snprintf(ui->hint, sizeof ui->hint, "%zu matches · Tab or Enter to choose",
                 ui->files.count);
    }
    free(prefix);
}

/* -------------------------------------------------------------- history */

static void history_move(tw_ui *ui, bool older)
{
    tw_history *h = &ui->history;
    if (h->count == 0)
        return;
    if (older) {
        if (ui->history_pos == h->count) {
            free(ui->draft);
            ui->draft = strdup(ui->line.buf);
        }
        if (ui->history_pos > 0)
            ui->history_pos--;
        tw_line_set(&ui->line, h->items[ui->history_pos]);
    } else {
        if (ui->history_pos >= h->count)
            return;
        ui->history_pos++;
        const char *text = (ui->history_pos < h->count) ? h->items[ui->history_pos] : ui->draft;
        tw_line_set(&ui->line, text != NULL ? text : "");
    }
    ui->menu = MENU_NONE;
    ui->menu_dismissed = true; /* do not pop the menu up while browsing */
}

static void search_update(tw_ui *ui, size_t before)
{
    size_t found = 0;
    ui->search_found =
        ui->query.len > 0 && tw_history_search(&ui->history, ui->query.buf, before, &found);
    if (ui->search_found) {
        ui->search_match = found;
        tw_line_set(&ui->line, ui->history.items[found]);
    }
}

static void search_start(tw_ui *ui)
{
    ui->searching = true;
    free(ui->search_saved);
    ui->search_saved = strdup(ui->line.buf);
    tw_line_clear(&ui->query);
    ui->search_found = false;
    ui->search_match = ui->history.count;
    ui->menu = MENU_NONE;
}

static void search_finish(tw_ui *ui, bool keep)
{
    ui->searching = false;
    if (!keep && ui->search_saved != NULL)
        tw_line_set(&ui->line, ui->search_saved);
    free(ui->search_saved);
    ui->search_saved = NULL;
    ui->menu_dismissed = true;
    ui->menu = MENU_NONE;
}

/* --------------------------------------------------------------- commands */

/* Runs a command line. Called and returns with ui->lock held. */
static void execute(tw_ui *ui, const char *typed)
{
    char *text = strdup(typed);
    if (text == NULL)
        return;
    tw_history_add(&ui->history, text);
    tw_line_clear(&ui->line);
    free(ui->draft);
    ui->draft = NULL;
    after_edit(ui);

    tw_strbuf echo;
    tw_strbuf_init(&echo);
    format_echo(ui, &echo, text);
    print_above(ui, tw_strbuf_str(&echo));
    tw_strbuf_free(&echo);

    char *command = text;
    while (*command == ' ' || *command == '\t')
        command++;
    if (*command == '/')
        command++;

    /* /threads toggles the per-thread panel of the live region. */
    if (strcmp(command, "threads") == 0) {
        ui->show_threads = !ui->show_threads;
        print_result(ui,
                     ui->show_threads
                         ? "Thread panel on: each thread's progress is shown during analysis.\n"
                         : "Thread panel off.\n",
                     "");
        free(text);
        return;
    }

    char *out_text = NULL;
    char *err_text = NULL;
    size_t out_len = 0;
    size_t err_len = 0;
    FILE *out = open_memstream(&out_text, &out_len);
    FILE *err = open_memstream(&err_text, &err_len);
    if (out == NULL || err == NULL) {
        if (out != NULL)
            fclose(out);
        if (err != NULL)
            fclose(err);
        free(out_text);
        free(err_text);
        print_result(ui, "", "error: out of memory\n");
        free(text);
        return;
    }

    tw_app *app = ui->app;
    FILE *saved_out = app->out;
    FILE *saved_err = app->err;
    app->out = out;
    app->err = err;

    pthread_mutex_unlock(&ui->lock);
    tw_cmd_result result = tw_command_execute(app, command);
    pthread_mutex_lock(&ui->lock);

    app->out = saved_out;
    app->err = saved_err;
    fclose(out);
    fclose(err);
    print_result(ui, out_text ? out_text : "", err_text ? err_text : "");
    free(out_text);
    free(err_text);
    free(text);

    if (ui->prefill != NULL) {
        tw_line_set(&ui->line, ui->prefill); /* "edit": change the line and press Enter */
        free(ui->prefill);
        ui->prefill = NULL;
        after_edit(ui);
    }
    if (result == TW_CMD_QUIT)
        ui->quit = true;
}

/* tw_notify_fn: a quiet notice as a toast. Called from the services thread. */
static void ui_notify(tw_app *app, const char *text)
{
    tw_ui *ui = app->ui;
    pthread_mutex_lock(&ui->lock);
    show_toast(ui, TW_TOAST_INFO, text);
    refresh(ui);
    pthread_mutex_unlock(&ui->lock);
}

/* tw_prefill_fn: keeps the text until the command returns (main thread). */
static void ui_prefill(tw_app *app, const char *text)
{
    tw_ui *ui = app->ui;
    free(ui->prefill);
    size_t len = strlen(text);
    ui->prefill = malloc(len + 2);
    if (ui->prefill != NULL) {
        ui->prefill[0] = '/'; /* commands are typed with a slash here */
        memcpy(ui->prefill + 1, text, len + 1);
    }
}

static void on_enter(tw_ui *ui)
{
    if (ui->menu == MENU_FILES) {
        accept_file(ui);
        return;
    }
    if (ui->menu == MENU_COMMANDS && ui->item_count > 0) {
        const char *typed = ui->line.buf + 1;
        if (command_named(typed, strlen(typed)) == NULL) {
            const tw_command_info *info = tw_command_at(ui->command_of[ui->selected]);
            if (takes_argument(info)) {
                accept_command(ui, ui->selected);
                return;
            }
            char text[LABEL_SIZE + 1];
            snprintf(text, sizeof text, "/%s", info->name);
            execute(ui, text);
            return;
        }
    }
    if (ui->line.len == 0)
        return;
    char *text = strdup(ui->line.buf);
    if (text != NULL) {
        execute(ui, text);
        free(text);
    }
}

static void on_ctrl_c(tw_ui *ui)
{
    if (ui->line.len > 0) {
        tw_line_clear(&ui->line);
        after_edit(ui);
        return;
    }
    double now = now_seconds();
    if (now - ui->ctrl_c_time < CTRL_C_WINDOW) {
        ui->ctrl_c_time = 0;
        execute(ui, "/quit");
        return;
    }
    ui->ctrl_c_time = now;
    snprintf(ui->hint, sizeof ui->hint, "Press Ctrl+C again to exit");
}

static void on_escape(tw_ui *ui)
{
    if (ui->menu != MENU_NONE) {
        close_file_menu(ui);
        ui->menu = MENU_NONE;
        ui->menu_dismissed = true;
        ui->hint[0] = '\0';
    } else if (tw_job_is_running(&ui->app->job)) {
        tw_job_cancel(&ui->app->job);
    } else {
        ui->hint[0] = '\0';
    }
}

/* ------------------------------------------------------------------ keys */

static bool handle_search_key(tw_ui *ui, const tw_key *key)
{
    switch (key->type) {
    case TW_KEY_TEXT:
        tw_line_insert(&ui->query, key->text);
        search_update(ui, ui->history.count);
        return true;
    case TW_KEY_BACKSPACE:
        tw_line_backspace(&ui->query);
        search_update(ui, ui->history.count);
        return true;
    case TW_KEY_ENTER:
        search_finish(ui, true);
        on_enter(ui);
        return true;
    case TW_KEY_ESCAPE:
        search_finish(ui, false);
        return true;
    case TW_KEY_CTRL:
        if (key->ctrl == 'r') {
            search_update(ui, ui->search_found ? ui->search_match : ui->history.count);
            return true;
        }
        if (key->ctrl == 'c' || key->ctrl == 'g') {
            search_finish(ui, false);
            return true;
        }
        break;
    default:
        break;
    }
    search_finish(ui, true); /* accept the match, then handle the key normally */
    return false;
}

static void handle_ctrl(tw_ui *ui, char c)
{
    switch (c) {
    case 'a':
        tw_line_home(&ui->line);
        break;
    case 'e':
        tw_line_end(&ui->line);
        break;
    case 'b':
        tw_line_left(&ui->line);
        break;
    case 'f':
        tw_line_right(&ui->line);
        break;
    case 'u':
        tw_line_kill_to_start(&ui->line);
        after_edit(ui);
        break;
    case 'k':
        tw_line_kill_to_end(&ui->line);
        after_edit(ui);
        break;
    case 'w':
        tw_line_kill_word(&ui->line);
        after_edit(ui);
        break;
    case 'p':
        history_move(ui, true);
        break;
    case 'n':
        history_move(ui, false);
        break;
    case 'r':
        search_start(ui);
        break;
    case 'l':
        tw_strbuf_append(&ui->out, "\x1b[2J\x1b[H");
        ui->live_drawn = false;
        break;
    case 'c':
        on_ctrl_c(ui);
        break;
    case 'd':
        if (ui->line.len == 0) {
            execute(ui, "/quit");
        } else {
            tw_line_delete(&ui->line);
            after_edit(ui);
        }
        break;
    default:
        break;
    }
}

static void handle_key(tw_ui *ui, const tw_key *key)
{
    if (key->type != TW_KEY_CTRL || key->ctrl != 'c')
        ui->ctrl_c_time = 0;
    if (ui->hint[0] != '\0' && strncmp(ui->hint, "Press Ctrl+C", 12) == 0 &&
        !(key->type == TW_KEY_CTRL && key->ctrl == 'c'))
        ui->hint[0] = '\0';

    if (ui->in_paste) {
        if (key->type == TW_KEY_PASTE_END)
            ui->in_paste = false;
        else if (key->type == TW_KEY_TEXT)
            tw_line_insert(&ui->line, key->text);
        else if (key->type == TW_KEY_ENTER || key->type == TW_KEY_TAB)
            tw_line_insert(&ui->line, " ");
        after_edit(ui);
        return;
    }

    if (ui->searching && handle_search_key(ui, key))
        return;

    bool menu_open = ui->menu != MENU_NONE && ui->item_count > 0;
    switch (key->type) {
    case TW_KEY_TEXT:
        if (key->text[0] == '?' && ui->line.len == 0) {
            print_shortcuts(ui);
            return;
        }
        tw_line_insert(&ui->line, key->text);
        after_edit(ui);
        break;
    case TW_KEY_PASTE_BEGIN:
        ui->in_paste = true;
        break;
    case TW_KEY_ENTER:
        on_enter(ui);
        break;
    case TW_KEY_TAB:
        if (ui->menu == MENU_COMMANDS && menu_open)
            accept_command(ui, ui->selected);
        else if (ui->menu == MENU_FILES && menu_open)
            accept_file(ui);
        else
            complete_file(ui);
        break;
    case TW_KEY_UP:
    case TW_KEY_SHIFT_TAB:
        if (menu_open)
            ui->selected = ui->selected == 0 ? ui->item_count - 1 : ui->selected - 1;
        else if (key->type == TW_KEY_UP)
            history_move(ui, true);
        break;
    case TW_KEY_DOWN:
        if (menu_open)
            ui->selected = (ui->selected + 1) % ui->item_count;
        else
            history_move(ui, false);
        break;
    case TW_KEY_BACKSPACE:
        tw_line_backspace(&ui->line);
        after_edit(ui);
        break;
    case TW_KEY_DELETE:
        tw_line_delete(&ui->line);
        after_edit(ui);
        break;
    case TW_KEY_LEFT:
        tw_line_left(&ui->line);
        break;
    case TW_KEY_RIGHT:
        tw_line_right(&ui->line);
        break;
    case TW_KEY_HOME:
        tw_line_home(&ui->line);
        break;
    case TW_KEY_END:
        tw_line_end(&ui->line);
        break;
    case TW_KEY_WORD_LEFT:
        tw_line_word_left(&ui->line);
        break;
    case TW_KEY_WORD_RIGHT:
        tw_line_word_right(&ui->line);
        break;
    case TW_KEY_ESCAPE:
        on_escape(ui);
        break;
    case TW_KEY_CTRL:
        handle_ctrl(ui, key->ctrl);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- main loop */

bool tw_ui_available(void)
{
    const char *term = getenv("TERM"); // NOLINT(concurrency-mt-unsafe): main thread only
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) &&
           !(term != NULL && strcmp(term, "dumb") == 0);
}

static bool ui_init(tw_ui *ui, tw_app *app, const tw_ui_options *options)
{
    memset(ui, 0, sizeof *ui);
    ui->app = app;
    ui->theme = options->theme ? options->theme : tw_theme_default();
    ui->color = options->color;
    ui->animate = options->animate;
    ui->start_time = now_seconds();
    ui->columns = 80;
    int rows = 0;
    tw_term_size(&ui->columns, &rows);
    tw_strbuf_init(&ui->out);
    tw_history_init(&ui->history, 1000);
    if (options->open_file != NULL) {
        ui->pending_open = strdup(options->open_file);
        if (ui->pending_open == NULL)
            return false;
    }
    if (pthread_mutex_init(&ui->lock, NULL) != 0)
        return false;
    if (pthread_cond_init(&ui->wake, NULL) != 0) {
        pthread_mutex_destroy(&ui->lock);
        return false;
    }
    if (!tw_line_init(&ui->line) || !tw_line_init(&ui->query)) {
        tw_line_free(&ui->line);
        pthread_cond_destroy(&ui->wake);
        pthread_mutex_destroy(&ui->lock);
        return false;
    }
    return true;
}

static void ui_free(tw_ui *ui)
{
    tw_line_free(&ui->line);
    tw_line_free(&ui->query);
    tw_history_free(&ui->history);
    tw_completions_free(&ui->files);
    free(ui->draft);
    free(ui->search_saved);
    free(ui->pending_open);
    free(ui->prefill);
    if (ui->prev_valid)
        tw_canvas_free(&ui->prev);
    tw_strbuf_free(&ui->out);
    pthread_cond_destroy(&ui->wake);
    pthread_mutex_destroy(&ui->lock);
}

/* ---------------------------------------------------------- render thread */

static bool wants_frames(tw_ui *ui)
{
    return ui->intro_active || ui->card_active || ui->toast_active ||
           tw_job_is_running(&ui->app->job) || atomic_load(&ui->app->sync_running);
}

static void sleep_seconds(double seconds)
{
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) != 0) {
    }
}

/*
 * Draws animation frames while something moves (intro, result card, toast,
 * running analysis) and sleeps on a condition variable otherwise, so an idle
 * TwinLine uses no CPU.
 */
static void *render_main(void *arg)
{
    tw_ui *ui = arg;
    pthread_mutex_lock(&ui->lock);
    while (!ui->stopping) {
        if (!wants_frames(ui)) {
            pthread_cond_wait(&ui->wake, &ui->lock);
            continue;
        }
        double now = now_seconds();
        if ((ui->intro_active && now - ui->intro_start >= INTRO_SECONDS) ||
            (ui->card_active && now - ui->card_start >= CARD_SECONDS))
            print_above(ui, ""); /* the animation is over: commit it */
        if (ui->toast_active && now - ui->toast_start >= TW_TOAST_SECONDS)
            ui->toast_active = false;
        refresh(ui);

        pthread_mutex_unlock(&ui->lock);
        sleep_seconds(ui->animate ? FRAME_SECONDS : SLOW_FRAME_SECONDS);
        pthread_mutex_lock(&ui->lock);
    }
    pthread_mutex_unlock(&ui->lock);
    return NULL;
}

/* ------------------------------------------------------------- main loop */

/* Handles the keys in `buf`; bytes of an unfinished sequence stay in it.
 * Caller holds ui->lock. */
static void process_input(tw_ui *ui, unsigned char *buf, size_t *len)
{
    if (ui->intro_active)
        print_above(ui, ""); /* a key press ends the intro at once */

    size_t pos = 0;
    while (pos < *len && !ui->quit) {
        tw_key key;
        size_t used = tw_key_decode(buf + pos, *len - pos, true, &key);
        if (used == 0) {
            if (tw_term_wait_input(30) == 1)
                break; /* the rest of the sequence is on its way */
            used = tw_key_decode(buf + pos, *len - pos, false, &key);
            if (used == 0)
                used = *len - pos;
        }
        pos += used;
        handle_key(ui, &key);
    }
    memmove(buf, buf + pos, *len - pos);
    *len -= pos;
}

bool tw_ui_run(tw_app *app, const tw_ui_options *options)
{
    if (!tw_ui_available())
        return false;
    tw_ui ui;
    if (!ui_init(&ui, app, options))
        return false;
    if (!tw_term_enable_raw()) {
        ui_free(&ui);
        return false;
    }

    app->ui = &ui;
    app->emit = ui_emit;
    app->on_result = ui_on_result;
    app->prefill = ui_prefill;
    app->notify = ui_notify;
    app->marks = true;

    pthread_mutex_lock(&ui.lock);
    start_intro(&ui);
    pthread_mutex_unlock(&ui.lock);
    ui.render_started = pthread_create(&ui.render_thread, NULL, render_main, &ui) == 0;

    unsigned char input[512];
    size_t input_len = 0;
    while (!ui.quit) {
        int ready = tw_term_wait_input(ui.pending_open != NULL ? 100 : 1000);

        /* Read outside the lock, so a background result is never held up. */
        if (ready == 1) {
            ssize_t n = read(STDIN_FILENO, input + input_len, sizeof input - input_len);
            if (n > 0)
                input_len += (size_t)n;
            else if (n == 0)
                ui.quit = true; /* the terminal was closed */
            else
                ready = 0;
        }

        pthread_mutex_lock(&ui.lock);
        bool redraw = false;
        if (tw_term_take_resize()) {
            int rows = 0;
            tw_term_size(&ui.columns, &rows);
            redraw = true;
        }
        if (ready == 1 && input_len > 0) {
            process_input(&ui, input, &input_len);
            redraw = true;
        }
        if (ui.pending_open != NULL && !ui.intro_active && !ui.quit) {
            char command[1100];
            snprintf(command, sizeof command, "/open %s", ui.pending_open);
            free(ui.pending_open);
            ui.pending_open = NULL;
            execute(&ui, command);
        }
        if (redraw && !ui.quit)
            refresh(&ui);
        flush(&ui);
        pthread_mutex_unlock(&ui.lock);
    }

    /* Stop the animations, then let a running worker finish reporting. */
    pthread_mutex_lock(&ui.lock);
    ui.stopping = true;
    pthread_cond_signal(&ui.wake);
    pthread_mutex_unlock(&ui.lock);
    if (ui.render_started)
        pthread_join(ui.render_thread, NULL);

    atomic_store(&app->quitting, true);
    if (tw_job_is_running(&app->job))
        tw_job_cancel(&app->job);
    tw_job_wait(&app->job);           /* the worker may still report through ui_emit */
    tw_app_stop_services(app, false); /* it reports through ui_emit and ui_notify too */

    pthread_mutex_lock(&ui.lock);
    ui.toast_active = false;
    if (ui.intro_active || ui.card_active)
        print_above(&ui, "");
    ui.closing = true;
    erase_live(&ui);
    flush(&ui);
    pthread_mutex_unlock(&ui.lock);

    tw_term_restore();
    app->emit = tw_app_emit_plain;
    app->on_result = NULL;
    app->prefill = NULL;
    app->notify = NULL;
    app->marks = false;
    app->ui = NULL;
    ui_free(&ui);
    return true;
}
