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

    bool live_drawn;
    int live_cursor_row;
    bool closing;
    bool quit;
    bool was_busy;
    int last_percent;
    tw_strbuf out;
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

static void erase_live(tw_ui *ui)
{
    if (!ui->live_drawn)
        return;
    tw_strbuf_append(&ui->out, "\r");
    if (ui->live_cursor_row > 0)
        tw_strbuf_appendf(&ui->out, "\x1b[%dA", ui->live_cursor_row);
    tw_strbuf_append(&ui->out, "\x1b[J");
    ui->live_drawn = false;
}

static void build_view(tw_ui *ui, tw_view *view, char *activity, size_t activity_size, char *title,
                       size_t title_size, char *file, size_t file_size)
{
    tw_app *app = ui->app;
    memset(view, 0, sizeof *view);
    view->theme = ui->theme;
    view->width = ui->columns - 1;
    view->input = ui->line.buf;
    view->cursor = ui->line.cursor;

    if (ui->searching) {
        snprintf(title, title_size, "%s: %s",
                 ui->search_found || ui->query.len == 0 ? "reverse search" : "no match for",
                 ui->query.buf);
        view->title = title;
    }

    if (tw_job_is_running(&app->job)) {
        view->busy = true;
        if (tw_job_is_cancelling(&app->job)) {
            snprintf(activity, activity_size, "Cancelling the analysis…");
        } else {
            int percent = (int)(100.0 * tw_analysis_progress_fraction(&app->job.progress));
            size_t threads = tw_analysis_thread_count(&app->options);
            snprintf(activity, activity_size, "Analyzing… %d%% · %zu thread%s · esc to cancel",
                     percent, threads, threads == 1 ? "" : "s");
        }
        view->activity = activity;
    }

    if (ui->menu != MENU_NONE) {
        view->items = ui->items;
        view->item_count = ui->item_count;
        view->selected = ui->selected;
    }
    view->hint = ui->hint[0] ? ui->hint : NULL;

    pthread_mutex_lock(&app->session.lock);
    if (app->session.path != NULL) {
        snprintf(file, file_size, "%s", app->session.path);
        view->file = file;
    }
    view->modified = app->session.dirty;
    view->lines = app->session.doc.count;
    pthread_mutex_unlock(&app->session.lock);

    view->threads = tw_analysis_thread_count(&app->options);
    view->exhaustive = app->options.exhaustive;
    view->turkish = app->options.fold == TW_FOLD_TURKISH;
}

static void draw_live(tw_ui *ui)
{
    if (ui->closing)
        return;
    char activity[160];
    char title[200];
    char file[512];
    tw_view view;
    build_view(ui, &view, activity, sizeof activity, title, sizeof title, file, sizeof file);

    tw_canvas canvas;
    int cx = 0;
    int cy = 0;
    if (!tw_view_render(&view, &canvas, &cx, &cy))
        return;

    tw_strbuf row;
    tw_strbuf_init(&row);
    for (int y = 0; y < canvas.height; y++) {
        tw_strbuf_clear(&row);
        tw_canvas_render_row(&canvas, y, ui->color, &row);
        tw_strbuf_append(&ui->out, tw_strbuf_str(&row));
        if (y + 1 < canvas.height)
            tw_strbuf_append(&ui->out, "\n");
    }
    tw_strbuf_free(&row);

    /* Move from the end of the last row to the input cursor. */
    int up = canvas.height - 1 - cy;
    if (up > 0)
        tw_strbuf_appendf(&ui->out, "\x1b[%dA", up);
    tw_strbuf_append(&ui->out, "\r");
    if (cx > 0)
        tw_strbuf_appendf(&ui->out, "\x1b[%dC", cx);

    ui->live_cursor_row = cy;
    ui->live_drawn = true;
    tw_canvas_free(&canvas);
}

/* Redraws the live region in place. Caller holds ui->lock. */
static void refresh(tw_ui *ui)
{
    tw_strbuf_append(&ui->out, HIDE_CURSOR);
    erase_live(ui);
    draw_live(ui);
    tw_strbuf_append(&ui->out, SHOW_CURSOR);
    flush(ui);
}

/* Prints `text` (ANSI, ending in a newline) above the live region. */
static void print_above(tw_ui *ui, const char *text)
{
    tw_strbuf_append(&ui->out, HIDE_CURSOR);
    erase_live(ui);
    tw_strbuf_append(&ui->out, text);
    draw_live(ui);
    tw_strbuf_append(&ui->out, SHOW_CURSOR);
    flush(ui);
}

/* ---------------------------------------------------------- formatting */

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
        tw_strbuf_append_n(sb, p, len);
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

static void print_welcome(tw_ui *ui)
{
    char cwd[1024];
    char shown[1024];
    if (getcwd(cwd, sizeof cwd) == NULL)
        snprintf(cwd, sizeof cwd, ".");
    const char *home = getenv("HOME"); // NOLINT(concurrency-mt-unsafe): main thread only
    size_t home_len = home ? strlen(home) : 0;
    if (home_len > 1 && strncmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '/' || cwd[home_len] == '\0'))
        snprintf(shown, sizeof shown, "~%s", cwd + home_len);
    else
        snprintf(shown, sizeof shown, "%s", cwd);

    tw_welcome welcome = {ui->theme, ui->columns - 1, TWINLINE_VERSION, shown};
    tw_canvas canvas;
    if (!tw_welcome_render(&welcome, &canvas))
        return;
    tw_strbuf sb;
    tw_strbuf_init(&sb);
    for (int y = 0; y < canvas.height; y++) {
        tw_canvas_render_row(&canvas, y, ui->color, &sb);
        tw_strbuf_append(&sb, "\n");
    }
    print_above(ui, tw_strbuf_str(&sb));
    tw_strbuf_free(&sb);
    tw_canvas_free(&canvas);
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
    print_above(ui, tw_strbuf_str(&sb));
    tw_strbuf_free(&sb);
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

    if (result == TW_CMD_QUIT)
        ui->quit = true;
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
    ui->columns = 80;
    int rows = 0;
    tw_term_size(&ui->columns, &rows);
    tw_strbuf_init(&ui->out);
    tw_history_init(&ui->history, 1000);
    ui->last_percent = -1;
    if (pthread_mutex_init(&ui->lock, NULL) != 0)
        return false;
    if (!tw_line_init(&ui->line) || !tw_line_init(&ui->query)) {
        tw_line_free(&ui->line);
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
    tw_strbuf_free(&ui->out);
    pthread_mutex_destroy(&ui->lock);
}

/* Handles the keys in `buf`; bytes of an unfinished sequence stay in it.
 * Caller holds ui->lock. */
static void process_input(tw_ui *ui, unsigned char *buf, size_t *len)
{
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

    pthread_mutex_lock(&ui.lock);
    print_welcome(&ui);
    if (options->open_file != NULL) {
        char command[1100];
        snprintf(command, sizeof command, "/open %s", options->open_file);
        execute(&ui, command);
    }
    refresh(&ui);
    pthread_mutex_unlock(&ui.lock);

    unsigned char input[512];
    size_t input_len = 0;
    while (!ui.quit) {
        bool busy = tw_job_is_running(&app->job);
        int ready = tw_term_wait_input(busy ? 120 : 1000);

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
        busy = tw_job_is_running(&app->job);
        int percent = busy ? (int)(100.0 * tw_analysis_progress_fraction(&app->job.progress)) : -1;
        if (busy != ui.was_busy || percent != ui.last_percent)
            redraw = true;
        ui.was_busy = busy;
        ui.last_percent = percent;
        if (redraw && !ui.quit)
            refresh(&ui);
        flush(&ui);
        pthread_mutex_unlock(&ui.lock);
    }

    /* Leave the live region on screen as it is, below the last output. */
    atomic_store(&app->quitting, true);
    if (tw_job_is_running(&app->job))
        tw_job_cancel(&app->job);
    tw_job_wait(&app->job); /* the worker may still report through ui_emit */

    pthread_mutex_lock(&ui.lock);
    ui.closing = true;
    erase_live(&ui);
    flush(&ui);
    pthread_mutex_unlock(&ui.lock);

    tw_term_restore();
    app->emit = tw_app_emit_plain;
    app->ui = NULL;
    ui_free(&ui);
    return true;
}
