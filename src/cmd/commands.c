/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/commands.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd/report.h"
#include "twinline/fileio.h"
#include "twinline/utf8.h"

#if defined(__GNUC__) || defined(__clang__)
#define TW_PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define TW_PRINTF_LIKE(fmt, args)
#endif

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static void print_error(tw_app *app, const char *fmt, ...) TW_PRINTF_LIKE(2, 3);

static void print_error(tw_app *app, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    flockfile(app->err);
    fputs("error: ", app->err);
    vfprintf(app->err, fmt, args);
    fputc('\n', app->err);
    funlockfile(app->err);
    va_end(args);
}

/* Describes a failure. Uses the thread-safe XSI strerror_r for errno values. */
static const char *describe(tw_status status, int err_no, char *buf, size_t buf_size)
{
    if (status != TW_ERR_IO || err_no == 0)
        return tw_status_str(status);
    if (strerror_r(err_no, buf, buf_size) != 0)
        return tw_status_str(status);
    return buf;
}

/* "1 line", "2 lines" */
static const char *plural(size_t n)
{
    return n == 1 ? "" : "s";
}

static bool session_is_dirty(tw_app *app)
{
    pthread_mutex_lock(&app->session.lock);
    bool dirty = app->session.dirty;
    pthread_mutex_unlock(&app->session.lock);
    return dirty;
}

/* ------------------------------------------------------------------------ */
/* Argument helpers                                                          */
/* ------------------------------------------------------------------------ */

/* Parses a positive decimal number at `*p` and advances past it. */
static bool parse_number(const char **p, size_t *out)
{
    const char *s = *p;
    if (*s < '0' || *s > '9')
        return false;
    size_t value = 0;
    while (*s >= '0' && *s <= '9') {
        size_t digit = (size_t)(*s - '0');
        if (value > (SIZE_MAX - digit) / 10)
            return false;
        value = value * 10 + digit;
        s++;
    }
    *p = s;
    *out = value;
    return true;
}

/*
 * Parses "N TEXT" (TEXT may be empty). `*text` points into `arg`.
 * Reports the problem and returns false on bad input.
 */
static bool parse_line_and_text(tw_app *app, const char *arg, const char *usage, size_t *line,
                                const char **text)
{
    const char *p = arg;
    if (!parse_number(&p, line) || (*p != '\0' && *p != ' ')) {
        print_error(app, "usage: %s", usage);
        return false;
    }
    *text = *p == ' ' ? p + 1 : p;
    return true;
}

/* Parses "N" or "N-M" into a 1-based inclusive range. */
static bool parse_range(const char *arg, size_t *first, size_t *last)
{
    const char *p = arg;
    if (!parse_number(&p, first))
        return false;
    *last = *first;
    if (*p == '-') {
        p++;
        if (!parse_number(&p, last))
            return false;
    }
    return *p == '\0';
}

/* Checks that lines first..last exist. Caller holds the session lock. */
static bool check_range_locked(tw_app *app, size_t first, size_t last)
{
    size_t count = app->session.doc.count;
    if (count == 0) {
        print_error(app, "the document is empty.");
        return false;
    }
    if (first == 0 || last < first || last > count) {
        if (first == last)
            print_error(app, "there is no line %zu; the document has %zu line%s.", first, count,
                        plural(count));
        else
            print_error(app, "lines %zu-%zu are not in the document (1-%zu).", first, last, count);
        return false;
    }
    return true;
}

static void print_line(tw_app *app, size_t index)
{
    fprintf(app->out, "%6zu  %s\n", index + 1, app->session.doc.lines[index]);
}

static const char *buffer_name(const char *path)
{
    return path != NULL ? path : "(untitled)";
}

/* ------------------------------------------------------------------------ */
/* Files                                                                     */
/* ------------------------------------------------------------------------ */

static bool load_file(tw_app *app, const char *path, tw_document *doc)
{
    int err_no = 0;
    tw_status status = tw_file_load(path, doc, &err_no);
    if (status != TW_OK) {
        char reason[128];
        print_error(app, "cannot open '%s': %s", path,
                    describe(status, err_no, reason, sizeof reason));
        return false;
    }
    return true;
}

void tw_command_open(tw_app *app, const char *path, bool force)
{
    if (!force && session_is_dirty(app)) {
        print_error(app, "unsaved changes. Save them first, or use 'open! %s' to discard them.",
                    path);
        return;
    }

    tw_document doc;
    if (!load_file(app, path, &doc))
        return;

    pthread_mutex_lock(&app->session.lock);
    tw_status status = tw_session_replace_locked(&app->session, &doc, path);
    size_t lines = app->session.doc.count;
    pthread_mutex_unlock(&app->session.lock);

    /* `doc` now holds the previous document (or the new one on failure).
     * Background workers use their own snapshot, so freeing it is safe. */
    tw_document_free(&doc);

    if (status != TW_OK) {
        print_error(app, "cannot open '%s': %s", path, tw_status_str(status));
        return;
    }
    fprintf(app->out, "Opened '%s' (%zu line%s).\n", path, lines, plural(lines));
}

static void cmd_open(tw_app *app, const char *arg, bool force)
{
    tw_command_open(app, arg, force);
}

static void cmd_save(tw_app *app, const char *arg, bool force)
{
    (void)force;
    pthread_mutex_lock(&app->session.lock);

    const char *path = (*arg != '\0') ? arg : app->session.path;
    if (path == NULL) {
        pthread_mutex_unlock(&app->session.lock);
        print_error(app, "no file name. Usage: save FILE");
        return;
    }

    int err_no = 0;
    tw_status status = tw_file_save(path, &app->session.doc, &err_no);
    if (status == TW_OK && path != app->session.path)
        status = tw_session_set_path_locked(&app->session, path);
    if (status == TW_OK)
        tw_session_mark_saved_locked(&app->session);
    size_t lines = app->session.doc.count;
    pthread_mutex_unlock(&app->session.lock);

    if (status != TW_OK) {
        char reason[128];
        print_error(app, "cannot save '%s': %s", *arg != '\0' ? arg : "the current file",
                    describe(status, err_no, reason, sizeof reason));
        return;
    }
    fprintf(app->out, "Saved %zu line%s.\n", lines, plural(lines));
}

/* ------------------------------------------------------------------------ */
/* Editing                                                                   */
/* ------------------------------------------------------------------------ */

/* Applies one edit; reports a failure. Caller holds the session lock. */
static bool edit_locked(tw_app *app, size_t at, size_t remove, const char *text)
{
    const char *texts[1] = {text};
    tw_status status =
        tw_session_edit_locked(&app->session, at, remove, texts, text != NULL ? 1 : 0);
    if (status != TW_OK) {
        print_error(app, "cannot change the document: %s", tw_status_str(status));
        return false;
    }
    return true;
}

static void cmd_append(tw_app *app, const char *arg, bool force)
{
    (void)force;
    pthread_mutex_lock(&app->session.lock);
    bool ok = edit_locked(app, app->session.doc.count, 0, arg);
    size_t lines = app->session.doc.count;
    pthread_mutex_unlock(&app->session.lock);
    if (ok)
        fprintf(app->out, "Line %zu added.\n", lines);
}

static void cmd_insert(tw_app *app, const char *arg, bool force)
{
    (void)force;
    size_t line = 0;
    const char *text = NULL;
    if (!parse_line_and_text(app, arg, "insert N TEXT", &line, &text))
        return;
    pthread_mutex_lock(&app->session.lock);
    size_t count = app->session.doc.count;
    bool ok = false;
    if (line == 0 || line > count + 1)
        print_error(app, "cannot insert at line %zu; use a line from 1 to %zu.", line, count + 1);
    else
        ok = edit_locked(app, line - 1, 0, text);
    pthread_mutex_unlock(&app->session.lock);
    if (ok)
        fprintf(app->out, "Line %zu inserted.\n", line);
}

static void cmd_delete(tw_app *app, const char *arg, bool force)
{
    (void)force;
    size_t first = 0;
    size_t last = 0;
    if (!parse_range(arg, &first, &last)) {
        print_error(app, "usage: delete N[-M]");
        return;
    }
    pthread_mutex_lock(&app->session.lock);
    bool ok =
        check_range_locked(app, first, last) && edit_locked(app, first - 1, last - first + 1, NULL);
    pthread_mutex_unlock(&app->session.lock);
    if (!ok)
        return;
    if (first == last)
        fprintf(app->out, "Line %zu deleted.\n", first);
    else
        fprintf(app->out, "Lines %zu-%zu deleted.\n", first, last);
}

static void cmd_replace(tw_app *app, const char *arg, bool force)
{
    (void)force;
    size_t line = 0;
    const char *text = NULL;
    if (!parse_line_and_text(app, arg, "replace N TEXT", &line, &text))
        return;
    pthread_mutex_lock(&app->session.lock);
    bool ok = check_range_locked(app, line, line) && edit_locked(app, line - 1, 1, text);
    pthread_mutex_unlock(&app->session.lock);
    if (ok)
        fprintf(app->out, "Line %zu replaced.\n", line);
}

static void cmd_edit(tw_app *app, const char *arg, bool force)
{
    (void)force;
    size_t line = 0;
    const char *p = arg;
    if (!parse_number(&p, &line) || *p != '\0') {
        print_error(app, "usage: edit N");
        return;
    }
    pthread_mutex_lock(&app->session.lock);
    char *command = NULL;
    if (check_range_locked(app, line, line)) {
        const char *text = app->session.doc.lines[line - 1];
        size_t size = strlen(text) + 32;
        command = malloc(size);
        if (command != NULL)
            snprintf(command, size, "replace %zu %s", line, text);
        else
            print_error(app, "cannot edit line %zu: %s", line, tw_status_str(TW_ERR_NOMEM));
    }
    if (command != NULL && app->prefill == NULL)
        print_line(app, line - 1);
    pthread_mutex_unlock(&app->session.lock);
    if (command == NULL)
        return;

    if (app->prefill != NULL) {
        app->prefill(app, command);
        fprintf(app->out, "Editing line %zu: change it and press Enter.\n", line);
    } else {
        fprintf(app->out, "To change it, type: replace %zu TEXT\n", line);
    }
    free(command);
}

static void cmd_undo(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    const tw_edit *edit = NULL;
    char what[96];
    pthread_mutex_lock(&app->session.lock);
    tw_status status = tw_session_undo_locked(&app->session, &edit);
    if (status == TW_OK)
        tw_edit_describe(edit, what, sizeof what);
    pthread_mutex_unlock(&app->session.lock);
    if (status == TW_ERR_INVALID)
        fprintf(app->out, "Nothing to undo.\n");
    else if (status != TW_OK)
        print_error(app, "cannot undo: %s", tw_status_str(status));
    else
        fprintf(app->out, "Undid the %s.\n", what);
}

static void cmd_redo(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    const tw_edit *edit = NULL;
    char what[96];
    pthread_mutex_lock(&app->session.lock);
    tw_status status = tw_session_redo_locked(&app->session, &edit);
    if (status == TW_OK)
        tw_edit_describe(edit, what, sizeof what);
    pthread_mutex_unlock(&app->session.lock);
    if (status == TW_ERR_INVALID)
        fprintf(app->out, "Nothing to redo.\n");
    else if (status != TW_OK)
        print_error(app, "cannot redo: %s", tw_status_str(status));
    else
        fprintf(app->out, "Redid the %s.\n", what);
}

/* ------------------------------------------------------------------------ */
/* Viewing                                                                   */
/* ------------------------------------------------------------------------ */

#define TW_PAGE_LINES 40 /* lines per "show" page */

/* Prints lines [from, to) and, when that is not the whole document, where
 * they are. Caller holds the session lock. */
static void print_lines_locked(tw_app *app, size_t from, size_t to)
{
    size_t count = app->session.doc.count;
    for (size_t i = from; i < to; i++)
        print_line(app, i);
    if (from == 0 && to == count)
        return;
    fprintf(app->out, "  Lines %zu-%zu of %zu.%s\n", from + 1, to, count,
            to < count ? " Type 'show' for the next page." : "");
}

static void cmd_show(tw_app *app, const char *arg, bool force)
{
    size_t first = 0;
    size_t last = 0;
    bool ranged = *arg != '\0';
    if (ranged && !parse_range(arg, &first, &last)) {
        print_error(app, "usage: show [N[-M]]");
        return;
    }

    pthread_mutex_lock(&app->session.lock);
    flockfile(app->out);
    size_t count = app->session.doc.count;
    if (count == 0 && !ranged) {
        fprintf(app->out, "%s\n", "(empty document)");
    } else if (force || (!ranged && count <= TW_PAGE_LINES)) {
        print_lines_locked(app, 0, count); /* show! prints everything */
    } else if (ranged && first != last) {
        if (check_range_locked(app, first, last)) {
            print_lines_locked(app, first - 1, last);
            app->session.page = last < count ? last : 0;
        }
    } else {
        /* A page: from line N, or where the previous page ended. */
        size_t from = app->session.page < count ? app->session.page : 0;
        if (ranged)
            from = check_range_locked(app, first, first) ? first - 1 : count;
        if (from < count) {
            size_t to = count - from > TW_PAGE_LINES ? from + TW_PAGE_LINES : count;
            print_lines_locked(app, from, to);
            app->session.page = to < count ? to : 0;
        }
    }
    funlockfile(app->out);
    pthread_mutex_unlock(&app->session.lock);
}

#define TW_GOTO_CONTEXT 3 /* lines shown on each side of the target */

static void cmd_goto(tw_app *app, const char *arg, bool force)
{
    (void)force;
    size_t line = 0;
    const char *p = arg;
    if (!parse_number(&p, &line) || *p != '\0') {
        print_error(app, "usage: goto N");
        return;
    }
    pthread_mutex_lock(&app->session.lock);
    if (check_range_locked(app, line, line)) {
        size_t count = app->session.doc.count;
        size_t from = line > TW_GOTO_CONTEXT ? line - 1 - TW_GOTO_CONTEXT : 0;
        size_t to = count - line > TW_GOTO_CONTEXT ? line + TW_GOTO_CONTEXT : count;
        for (size_t i = from; i < to; i++) {
            const char *text = app->session.doc.lines[i];
            if (i + 1 == line)
                fprintf(app->out, "> %4zu  %s%s%s\n", i + 1, app->marks ? TW_MARK_ON : "", text,
                        app->marks ? TW_MARK_OFF : "");
            else
                print_line(app, i);
        }
        app->session.page = line - 1; /* "show" continues from here */
    }
    pthread_mutex_unlock(&app->session.lock);
}

/* Folds `text` into code points for matching. Returns the count, or
 * SIZE_MAX when the text is longer than `max`. */
static size_t fold_text(const char *text, tw_fold_mode mode, uint32_t *out, size_t max)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p != '\0') {
        if (n == max)
            return SIZE_MAX;
        uint32_t cp = 0;
        p += tw_utf8_decode(p, &cp);
        out[n++] = tw_utf8_fold(cp, mode);
    }
    return n;
}

/* Bytes of `s` that match `needle` from its start, or 0. */
static size_t match_at(const char *s, const uint32_t *needle, size_t n, tw_fold_mode mode)
{
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++) {
        if (*p == '\0')
            return 0;
        uint32_t cp = 0;
        p += tw_utf8_decode(p, &cp);
        if (tw_utf8_fold(cp, mode) != needle[i])
            return 0;
    }
    return (size_t)((const char *)p - s);
}

/* Prints a line with its matches highlighted; returns the number of matches. */
static size_t print_matches(tw_app *app, size_t index, const uint32_t *needle, size_t n)
{
    const char *line = app->session.doc.lines[index];
    tw_fold_mode mode = app->options.fold;
    size_t matches = 0;
    const char *p = line;
    const char *printed = line;
    while (*p != '\0') {
        size_t len = match_at(p, needle, n, mode);
        if (len == 0) {
            uint32_t cp = 0;
            p += tw_utf8_decode((const unsigned char *)p, &cp);
            continue;
        }
        if (matches == 0)
            fprintf(app->out, "%6zu  ", index + 1);
        fprintf(app->out, "%.*s%s%.*s%s", (int)(p - printed), printed, app->marks ? TW_MARK_ON : "",
                (int)len, p, app->marks ? TW_MARK_OFF : "");
        matches++;
        p += len;
        printed = p;
    }
    if (matches > 0)
        fprintf(app->out, "%s\n", printed);
    return matches;
}

/* Number of matches of `needle` in `line`. */
static size_t count_matches(const char *line, const uint32_t *needle, size_t n, tw_fold_mode mode)
{
    size_t matches = 0;
    for (const char *p = line; *p != '\0';) {
        size_t len = match_at(p, needle, n, mode);
        if (len > 0) {
            matches++;
            p += len;
        } else {
            uint32_t cp = 0;
            p += tw_utf8_decode((const unsigned char *)p, &cp);
        }
    }
    return matches;
}

#define TW_FIND_MAX_CHARS 256 /* longest search text */
#define TW_FIND_MAX_LINES 100 /* matching lines printed */

static void cmd_find(tw_app *app, const char *arg, bool force)
{
    (void)force;
    uint32_t needle[TW_FIND_MAX_CHARS];
    size_t n = fold_text(arg, app->options.fold, needle, TW_FIND_MAX_CHARS);
    if (n == 0) {
        print_error(app, "usage: find TEXT");
        return;
    }
    if (n == SIZE_MAX) {
        print_error(app, "the search text is too long (at most %d characters).", TW_FIND_MAX_CHARS);
        return;
    }

    size_t matches = 0;
    size_t lines = 0;
    pthread_mutex_lock(&app->session.lock);
    flockfile(app->out);
    for (size_t i = 0; i < app->session.doc.count; i++) {
        size_t found = lines < TW_FIND_MAX_LINES
                           ? print_matches(app, i, needle, n)
                           : count_matches(app->session.doc.lines[i], needle, n, app->options.fold);
        matches += found;
        if (found > 0)
            lines++;
    }
    if (matches == 0)
        fprintf(app->out, "No line contains '%s'.\n", arg);
    else if (lines > TW_FIND_MAX_LINES)
        fprintf(app->out, "  %zu matches in %zu lines; the first %d are shown.\n", matches, lines,
                TW_FIND_MAX_LINES);
    else
        fprintf(app->out, "  %zu match%s in %zu line%s.\n", matches, matches == 1 ? "" : "es",
                lines, plural(lines));
    funlockfile(app->out);
    pthread_mutex_unlock(&app->session.lock);
}

/* ------------------------------------------------------------------------ */
/* Buffers                                                                   */
/* ------------------------------------------------------------------------ */

static void cmd_buffers(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    pthread_mutex_lock(&app->session.lock);
    for (size_t i = 0; i < app->buffer_count; i++) {
        bool active = i == app->current;
        const char *path = active ? app->session.path : app->buffers[i].path;
        size_t lines = active ? app->session.doc.count : app->buffers[i].doc.count;
        bool dirty = active ? app->session.dirty : app->buffers[i].dirty;
        fprintf(app->out, "%s %2zu  %-28s %6zu line%s%s\n", active ? ">" : " ", i + 1,
                buffer_name(path), lines, plural(lines), dirty ? "  modified" : "");
    }
    pthread_mutex_unlock(&app->session.lock);
}

static void cmd_new(tw_app *app, const char *arg, bool force)
{
    (void)force;
    tw_document doc;
    tw_document_init(&doc);
    if (*arg != '\0' && !load_file(app, arg, &doc))
        return;

    pthread_mutex_lock(&app->session.lock);
    tw_buffer *buffers = realloc(app->buffers, (app->buffer_count + 1) * sizeof *app->buffers);
    if (buffers == NULL) {
        pthread_mutex_unlock(&app->session.lock);
        tw_document_free(&doc);
        print_error(app, "cannot open a new buffer: %s", tw_status_str(TW_ERR_NOMEM));
        return;
    }
    app->buffers = buffers;
    /* Park the active buffer in its slot; the session gets the empty placeholder. */
    tw_session_exchange_locked(&app->session, &app->buffers[app->current]);
    app->current = app->buffer_count++;
    tw_buffer_init(&app->buffers[app->current]);

    tw_status status = TW_OK;
    if (*arg != '\0')
        status = tw_session_replace_locked(&app->session, &doc, arg);
    size_t number = app->current + 1;
    size_t lines = app->session.doc.count;
    pthread_mutex_unlock(&app->session.lock);
    tw_document_free(&doc);

    if (status != TW_OK)
        print_error(app, "cannot open '%s': %s", arg, tw_status_str(status));
    else if (*arg != '\0')
        fprintf(app->out, "Buffer %zu: opened '%s' (%zu line%s).\n", number, arg, lines,
                plural(lines));
    else
        fprintf(app->out, "Buffer %zu: new empty document.\n", number);
}

static void cmd_switch(tw_app *app, const char *arg, bool force)
{
    (void)force;
    size_t number = 0;
    const char *p = arg;
    if (!parse_number(&p, &number) || *p != '\0') {
        print_error(app, "usage: switch N");
        return;
    }
    pthread_mutex_lock(&app->session.lock);
    size_t count = app->buffer_count;
    bool ok = number >= 1 && number <= count && number - 1 != app->current;
    if (ok) {
        tw_session_exchange_locked(&app->session, &app->buffers[app->current]);
        app->current = number - 1;
        tw_session_exchange_locked(&app->session, &app->buffers[app->current]);
    }
    const char *name = buffer_name(app->session.path);
    size_t lines = app->session.doc.count;
    if (ok)
        fprintf(app->out, "Switched to buffer %zu: %s (%zu line%s).\n", number, name, lines,
                plural(lines));
    pthread_mutex_unlock(&app->session.lock);

    if (ok)
        return;
    if (number >= 1 && number <= count)
        fprintf(app->out, "Already in buffer %zu.\n", number);
    else
        print_error(app, "there is no buffer %zu; %zu %s open. Type 'buffers' to list them.",
                    number, count, count == 1 ? "is" : "are");
}

static void cmd_close(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    if (!force && session_is_dirty(app)) {
        print_error(app, "this buffer has unsaved changes. Save them, or use 'close!' to discard "
                         "them.");
        return;
    }

    pthread_mutex_lock(&app->session.lock);
    bool had_more = app->buffer_count > 1;
    char *name = strdup(buffer_name(app->session.path));

    /* Take the active buffer out of the session and drop it. */
    tw_buffer gone;
    tw_buffer_init(&gone);
    tw_session_exchange_locked(&app->session, &gone);
    tw_buffer_free(&gone);

    if (app->buffer_count > 1) {
        tw_buffer_free(&app->buffers[app->current]); /* the placeholder */
        memmove(app->buffers + app->current, app->buffers + app->current + 1,
                (app->buffer_count - app->current - 1) * sizeof *app->buffers);
        app->buffer_count--;
        if (app->current == app->buffer_count)
            app->current--;
        tw_session_exchange_locked(&app->session, &app->buffers[app->current]);
    }
    size_t now = app->current + 1;
    const char *now_name = buffer_name(app->session.path);
    if (had_more)
        fprintf(app->out, "Closed %s. Now in buffer %zu: %s.\n", name ? name : "the buffer", now,
                now_name);
    else
        fprintf(app->out, "Closed %s; the buffer is now empty.\n", name ? name : "the buffer");
    pthread_mutex_unlock(&app->session.lock);
    free(name);
}

/* ------------------------------------------------------------------------ */
/* Analysis                                                                  */
/* ------------------------------------------------------------------------ */

static bool take_snapshot(tw_app *app, tw_document *snapshot)
{
    tw_status status = tw_session_snapshot(&app->session, snapshot);
    if (status != TW_OK) {
        print_error(app, "cannot copy the document: %s", tw_status_str(status));
        return false;
    }
    if (snapshot->count == 0) {
        tw_document_free(snapshot);
        print_error(app, "the document is empty. Use 'open' or 'append' first.");
        return false;
    }
    return true;
}

static void cmd_analyze_sync(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    tw_document snapshot;
    if (!take_snapshot(app, &snapshot))
        return;

    tw_analysis_result result;
    tw_analysis_progress_reset(&app->sync_progress);
    atomic_store(&app->sync_running, true);
    tw_status status =
        tw_analysis_run(&snapshot, &app->options, NULL, &app->sync_progress, &result);
    atomic_store(&app->sync_running, false);
    tw_document_free(&snapshot);

    if (status != TW_OK) {
        print_error(app, "analysis failed: %s", tw_status_str(status));
        return;
    }
    if (app->on_result != NULL)
        app->on_result(app, &result);
    else
        tw_report_print(app->out, "Analysis finished:", &result);
}

/* Runs on the worker thread: formats the outcome and hands it to the front end. */
static void on_background_done(tw_status status, const tw_analysis_result *result, void *user)
{
    tw_app *app = user;
    if (status == TW_OK && app->on_result != NULL) {
        app->on_result(app, result);
        return;
    }
    char *text = NULL;
    size_t len = 0;
    FILE *buf = open_memstream(&text, &len);
    if (buf == NULL) {
        app->emit(app, "Background analysis finished (no memory for the report).\n", true);
        return;
    }

    if (status == TW_OK)
        tw_report_print(buf, "Background analysis finished:", result);
    else if (status == TW_ERR_CANCELLED)
        fputs("Background analysis cancelled.\n", buf);
    else
        fprintf(buf, "Background analysis failed: %s\n", tw_status_str(status));
    fclose(buf);

    app->emit(app, text, status != TW_OK && status != TW_ERR_CANCELLED);
    free(text);
}

static void cmd_analyze(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    if (tw_job_is_cancelling(&app->job)) {
        print_error(app, "the previous analysis is still stopping. Try again in a moment.");
        return;
    }
    if (tw_job_is_running(&app->job)) {
        print_error(app, "an analysis is already running. Use 'cancel' to stop it.");
        return;
    }

    tw_document snapshot;
    if (!take_snapshot(app, &snapshot))
        return;
    size_t lines = snapshot.count;

    tw_status status = tw_job_start(&app->job, &snapshot, &app->options, on_background_done, app);
    if (status != TW_OK) {
        print_error(app, "cannot start the analysis: %s", tw_status_str(status));
        return;
    }
    fprintf(app->out, "Analyzing %zu line%s in the background. Use 'status' or 'cancel'.\n", lines,
            plural(lines));
}

static void cmd_cancel(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    if (!tw_job_is_running(&app->job)) {
        fprintf(app->out, "%s\n", "No analysis is running.");
        return;
    }
    tw_job_cancel(&app->job);
    fprintf(app->out, "%s\n", "Cancelling the background analysis...");
}

static void cmd_status(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    pthread_mutex_lock(&app->session.lock);
    const char *path = app->session.path ? app->session.path : "(no file)";
    fprintf(app->out, "File     : %s\n", path);
    fprintf(app->out, "Lines    : %zu\n", app->session.doc.count);
    fprintf(app->out, "Modified : %s\n", app->session.dirty ? "yes" : "no");
    if (app->session.doc.map != NULL)
        fprintf(app->out, "%s\n", "Storage  : memory-mapped (large-file mode)");
    if (app->buffer_count > 1)
        fprintf(app->out, "Buffer   : %zu of %zu\n", app->current + 1, app->buffer_count);
    pthread_mutex_unlock(&app->session.lock);

    fprintf(app->out, "Folding  : %s\n",
            app->options.fold == TW_FOLD_TURKISH ? "turkish" : "default");
    size_t threads = tw_analysis_thread_count(&app->options);
    fprintf(app->out, "Threads  : %zu%s\n", threads,
            app->options.threads == 0 ? " (one per processor)" : "");
    fprintf(app->out, "Mode     : %s\n", app->options.exhaustive ? "exhaustive" : "pruned");
    if (tw_job_is_running(&app->job))
        fprintf(app->out, "Analysis : running, %.0f%% done\n",
                100.0 * tw_analysis_progress_fraction(&app->job.progress));
    else
        fprintf(app->out, "%s\n", "Analysis : idle");
}

static void cmd_threads(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    if (!tw_job_is_running(&app->job)) {
        fprintf(app->out, "No analysis is running.\n");
        return;
    }
    double fractions[TW_PROGRESS_SLOTS];
    size_t count = tw_analysis_progress_threads(&app->job.progress, fractions, TW_PROGRESS_SLOTS);
    for (size_t t = 0; t < count; t++)
        fprintf(app->out, "Thread %2zu : %3.0f%%\n", t + 1, 100.0 * fractions[t]);
}

static void cmd_help(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    tw_command_print_help(app->out);
}

/* ------------------------------------------------------------------------ */
/* Command table                                                             */
/* ------------------------------------------------------------------------ */

typedef enum arg_kind {
    ARGS_NONE,
    ARGS_FILE,
    ARGS_OPTIONAL_FILE,
    ARGS_TEXT,          /* free text, kept as typed */
    ARGS_WORD,          /* one required word without blanks (a number, a range) */
    ARGS_WORD_OPTIONAL, /* the same, optional */
} arg_kind;

typedef void (*command_fn)(tw_app *app, const char *arg, bool force);

typedef struct command_spec {
    tw_command_info info;
    arg_kind args;
    bool forceable; /* accepts a trailing '!' */
    command_fn run; /* NULL for quit/exit, handled by the dispatcher */
} command_spec;

static const command_spec COMMANDS[] = {
    /* Files and buffers */
    {{"open", "open[!] FILE", "Load FILE (use open! to discard unsaved changes)", true},
     ARGS_FILE,
     true,
     cmd_open},
    {{"save", "save [FILE]", "Save atomically to FILE or to the current file", true},
     ARGS_OPTIONAL_FILE,
     false,
     cmd_save},
    {{"new", "new [FILE]", "Open a new buffer, empty or with FILE", true},
     ARGS_OPTIONAL_FILE,
     false,
     cmd_new},
    {{"buffers", "buffers", "List the open buffers", false}, ARGS_NONE, false, cmd_buffers},
    {{"switch", "switch N", "Make buffer N the active one", false}, ARGS_WORD, false, cmd_switch},
    {{"close", "close[!]", "Close the active buffer (close! discards changes)", false},
     ARGS_NONE,
     true,
     cmd_close},
    /* Editing */
    {{"append", "append TEXT", "Add TEXT as a new last line", false}, ARGS_TEXT, false, cmd_append},
    {{"insert", "insert N TEXT", "Insert TEXT as line N", false}, ARGS_TEXT, false, cmd_insert},
    {{"delete", "delete N[-M]", "Delete line N, or lines N to M", false},
     ARGS_WORD,
     false,
     cmd_delete},
    {{"replace", "replace N TEXT", "Replace line N with TEXT", false},
     ARGS_TEXT,
     false,
     cmd_replace},
    {{"edit", "edit N", "Put line N in the input box to change it", false},
     ARGS_WORD,
     false,
     cmd_edit},
    {{"undo", "undo", "Undo the last change", false}, ARGS_NONE, false, cmd_undo},
    {{"redo", "redo", "Redo the last undone change", false}, ARGS_NONE, false, cmd_redo},
    /* Viewing */
    {{"show", "show[!] [N[-M]]", "Print a page (from line N) or lines N-M; show! prints all",
      false},
     ARGS_WORD_OPTIONAL,
     true,
     cmd_show},
    {{"goto", "goto N", "Show line N in context; show continues from it", false},
     ARGS_WORD,
     false,
     cmd_goto},
    {{"find", "find TEXT", "List the lines containing TEXT (case-insensitive)", false},
     ARGS_TEXT,
     false,
     cmd_find},
    /* Analysis */
    {{"analyze", "analyze", "Find near-duplicate lines in the background", false},
     ARGS_NONE,
     false,
     cmd_analyze},
    {{"analyze-sync", "analyze-sync", "Find near-duplicate lines and wait for the result", false},
     ARGS_NONE,
     false,
     cmd_analyze_sync},
    {{"cancel", "cancel", "Stop the running background analysis", false},
     ARGS_NONE,
     false,
     cmd_cancel},
    {{"status", "status", "Show the file, unsaved changes and analysis progress", false},
     ARGS_NONE,
     false,
     cmd_status},
    {{"threads", "threads", "Show the progress of each analysis thread", false},
     ARGS_NONE,
     false,
     cmd_threads},
    {{"help", "help", "Show this list", false}, ARGS_NONE, false, cmd_help},
    {{"quit", "quit[!]", "Exit (quit! discards unsaved changes)", false}, ARGS_NONE, true, NULL},
    {{"exit", "exit[!]", "Same as quit", false}, ARGS_NONE, true, NULL},
};

#define COMMAND_COUNT (sizeof COMMANDS / sizeof COMMANDS[0])

void tw_command_print_help(FILE *out)
{
    fputs("Commands:\n", out);
    for (size_t i = 0; i < COMMAND_COUNT; i++)
        fprintf(out, "  %-16s %s\n", COMMANDS[i].info.usage, COMMANDS[i].info.summary);
}

size_t tw_command_count(void)
{
    return COMMAND_COUNT;
}

const tw_command_info *tw_command_at(size_t index)
{
    return index < COMMAND_COUNT ? &COMMANDS[index].info : NULL;
}

static const command_spec *find_command(const char *name)
{
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(COMMANDS[i].info.name, name) == 0)
            return &COMMANDS[i];
    }
    return NULL;
}

static void trim_trailing_space(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = '\0';
}

static bool validate_args(tw_app *app, const command_spec *spec, char *arg)
{
    if (spec->args != ARGS_TEXT)
        trim_trailing_space(arg);

    switch (spec->args) {
    case ARGS_NONE:
        if (*arg != '\0') {
            print_error(app, "'%s' takes no arguments.", spec->info.name);
            return false;
        }
        return true;
    case ARGS_FILE:
        if (*arg == '\0') {
            print_error(app, "usage: %s", spec->info.usage);
            return false;
        }
        /* fall through */
    case ARGS_OPTIONAL_FILE:
        if (strpbrk(arg, " \t") != NULL) {
            print_error(app, "file names with spaces are not supported yet.");
            return false;
        }
        return true;
    case ARGS_WORD:
        if (*arg == '\0') {
            print_error(app, "usage: %s", spec->info.usage);
            return false;
        }
        /* fall through */
    case ARGS_WORD_OPTIONAL:
        if (strpbrk(arg, " \t") != NULL) {
            print_error(app, "usage: %s", spec->info.usage);
            return false;
        }
        return true;
    case ARGS_TEXT:
        return true;
    }
    return false;
}

static tw_cmd_result request_quit(tw_app *app, bool force)
{
    if (force || app->quit_armed)
        return TW_CMD_QUIT;
    size_t dirty = tw_app_dirty_buffers(app);
    if (dirty == 0)
        return TW_CMD_QUIT;

    app->quit_armed = true;
    if (dirty == 1 && !session_is_dirty(app))
        fputs("warning: another buffer has unsaved changes. Type 'quit' again to discard them, "
              "or 'buffers' to see it.\n",
              app->err);
    else if (dirty > 1)
        fprintf(app->err,
                "warning: %zu buffers have unsaved changes. Type 'quit' again to discard them, "
                "or 'buffers' to see them.\n",
                dirty);
    else
        fputs("warning: you have unsaved changes. Type 'quit' again to discard them, or 'save' "
              "first.\n",
              app->err);
    return TW_CMD_CONTINUE;
}

static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

bool tw_command_parse(char *line, tw_parsed_command *out)
{
    char *name = line;
    while (is_blank(*name))
        name++;
    if (*name == '\0')
        return false;

    char *arg = name;
    while (*arg != '\0' && !is_blank(*arg))
        arg++;
    if (*arg != '\0')
        *arg++ = '\0';
    while (is_blank(*arg))
        arg++;

    bool force = false;
    size_t name_len = strlen(name);
    if (name_len > 1 && name[name_len - 1] == '!') {
        name[name_len - 1] = '\0';
        force = true;
    }

    out->name = name;
    out->arg = arg;
    out->force = force;
    return true;
}

tw_cmd_result tw_command_execute(tw_app *app, char *line)
{
    tw_parsed_command cmd;
    if (!tw_command_parse(line, &cmd))
        return TW_CMD_CONTINUE;

    const command_spec *spec = find_command(cmd.name);
    if (spec == NULL || (cmd.force && !spec->forceable)) {
        print_error(app, "unknown command '%s%s'. Type 'help' for the list.", cmd.name,
                    cmd.force ? "!" : "");
        return TW_CMD_CONTINUE;
    }

    bool quitting = spec->run == NULL;
    if (!quitting)
        app->quit_armed = false;

    if (!validate_args(app, spec, cmd.arg))
        return TW_CMD_CONTINUE;

    if (quitting)
        return request_quit(app, cmd.force);

    spec->run(app, cmd.arg, cmd.force);
    return TW_CMD_CONTINUE;
}
