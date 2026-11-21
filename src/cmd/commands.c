/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/commands.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd/report.h"
#include "twinline/fileio.h"

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
/* Commands                                                                  */
/* ------------------------------------------------------------------------ */

void tw_command_open(tw_app *app, const char *path, bool force)
{
    if (!force && session_is_dirty(app)) {
        print_error(app, "unsaved changes. Save them first, or use 'open! %s' to discard them.",
                    path);
        return;
    }

    tw_document doc;
    int err_no = 0;
    tw_status status = tw_file_load(path, &doc, &err_no);
    if (status != TW_OK) {
        char reason[128];
        print_error(app, "cannot open '%s': %s", path,
                    describe(status, err_no, reason, sizeof reason));
        return;
    }

    pthread_mutex_lock(&app->session.lock);
    status = tw_session_set_path_locked(&app->session, path);
    if (status == TW_OK) {
        tw_document_swap(&app->session.doc, &doc);
        app->session.dirty = false;
    }
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

static void cmd_append(tw_app *app, const char *arg, bool force)
{
    (void)force;
    pthread_mutex_lock(&app->session.lock);
    tw_status status = tw_document_append(&app->session.doc, arg);
    if (status == TW_OK)
        app->session.dirty = true;
    size_t lines = app->session.doc.count;
    pthread_mutex_unlock(&app->session.lock);

    if (status != TW_OK) {
        print_error(app, "cannot append line: %s", tw_status_str(status));
        return;
    }
    fprintf(app->out, "Line %zu added.\n", lines);
}

static void cmd_show(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    pthread_mutex_lock(&app->session.lock);
    flockfile(app->out);
    if (app->session.doc.count == 0)
        fprintf(app->out, "%s\n", "(empty document)");
    for (size_t i = 0; i < app->session.doc.count; i++)
        fprintf(app->out, "%6zu  %s\n", i + 1, app->session.doc.lines[i]);
    funlockfile(app->out);
    pthread_mutex_unlock(&app->session.lock);
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
        app->session.dirty = false;
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
    tw_status status = tw_analysis_run(&snapshot, &app->options, NULL, NULL, &result);
    tw_document_free(&snapshot);

    if (status != TW_OK) {
        print_error(app, "analysis failed: %s", tw_status_str(status));
        return;
    }
    tw_report_print(app->out, "Analysis finished:", &result);
}

/* Runs on the worker thread: formats the outcome and hands it to the front end. */
static void on_background_done(tw_status status, const tw_analysis_result *result, void *user)
{
    tw_app *app = user;
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
    fprintf(app->out, "Analysing %zu line%s in the background. Use 'status' or 'cancel'.\n", lines,
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
    ARGS_TEXT,
} arg_kind;

typedef void (*command_fn)(tw_app *app, const char *arg, bool force);

typedef struct command_spec {
    tw_command_info info;
    arg_kind args;
    bool forceable; /* accepts a trailing '!' */
    command_fn run; /* NULL for quit/exit, handled by the dispatcher */
} command_spec;

static const command_spec COMMANDS[] = {
    {{"open", "open[!] FILE", "Load FILE (use open! to discard unsaved changes)", true},
     ARGS_FILE,
     true,
     cmd_open},
    {{"append", "append TEXT", "Add TEXT as a new last line", false}, ARGS_TEXT, false, cmd_append},
    {{"show", "show", "Print the document with line numbers", false}, ARGS_NONE, false, cmd_show},
    {{"save", "save [FILE]", "Save atomically to FILE or to the current file", true},
     ARGS_OPTIONAL_FILE,
     false,
     cmd_save},
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
    case ARGS_TEXT:
        return true;
    }
    return false;
}

static tw_cmd_result request_quit(tw_app *app, bool force)
{
    if (force || app->quit_armed || !session_is_dirty(app))
        return TW_CMD_QUIT;

    app->quit_armed = true;
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
