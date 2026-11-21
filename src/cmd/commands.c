/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/commands.h"

#include <stdarg.h>
#include <stdio.h>
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

static void print_error(const char *fmt, ...) TW_PRINTF_LIKE(1, 2);

static void print_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    flockfile(stderr);
    fputs("error: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    funlockfile(stderr);
    va_end(args);
}

static const char *describe(tw_status status, int err_no)
{
    return (status == TW_ERR_IO && err_no != 0) ? strerror(err_no) : tw_status_str(status);
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
        print_error("unsaved changes. Save them first, or use 'open! %s' to discard them.", path);
        return;
    }

    tw_document doc;
    int err_no = 0;
    tw_status status = tw_file_load(path, &doc, &err_no);
    if (status != TW_OK) {
        print_error("cannot open '%s': %s", path, describe(status, err_no));
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
        print_error("cannot open '%s': %s", path, tw_status_str(status));
        return;
    }
    printf("Opened '%s' (%zu lines).\n", path, lines);
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
        print_error("cannot append line: %s", tw_status_str(status));
        return;
    }
    printf("Line %zu added.\n", lines);
}

static void cmd_show(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    pthread_mutex_lock(&app->session.lock);
    flockfile(stdout);
    if (app->session.doc.count == 0)
        puts("(empty document)");
    for (size_t i = 0; i < app->session.doc.count; i++)
        printf("%6zu  %s\n", i + 1, app->session.doc.lines[i]);
    funlockfile(stdout);
    pthread_mutex_unlock(&app->session.lock);
}

static void cmd_save(tw_app *app, const char *arg, bool force)
{
    (void)force;
    pthread_mutex_lock(&app->session.lock);

    const char *path = (*arg != '\0') ? arg : app->session.path;
    if (path == NULL) {
        pthread_mutex_unlock(&app->session.lock);
        print_error("no file name. Usage: save FILE");
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
        print_error("cannot save '%s': %s", *arg != '\0' ? arg : "the current file",
                    describe(status, err_no));
        return;
    }
    printf("Saved %zu lines.\n", lines);
}

static bool take_snapshot(tw_app *app, tw_document *snapshot)
{
    tw_status status = tw_session_snapshot(&app->session, snapshot);
    if (status != TW_OK) {
        print_error("cannot copy the document: %s", tw_status_str(status));
        return false;
    }
    if (snapshot->count == 0) {
        tw_document_free(snapshot);
        print_error("the document is empty. Use 'open' or 'append' first.");
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
        print_error("analysis failed: %s", tw_status_str(status));
        return;
    }
    tw_report_print(stdout, "Analysis finished:", &result);
}

/* Runs on the worker thread. */
static void on_background_done(tw_status status, const tw_analysis_result *result, void *user)
{
    tw_app *app = user;

    flockfile(stdout);
    fputc('\n', stdout);
    if (status == TW_OK)
        tw_report_print(stdout, "Background analysis finished:", result);
    else if (status == TW_ERR_CANCELLED)
        puts("Background analysis cancelled.");
    else
        printf("Background analysis failed: %s\n", tw_status_str(status));
    if (!atomic_load(&app->quitting))
        fputs(TW_PROMPT, stdout);
    fflush(stdout);
    funlockfile(stdout);
}

static void cmd_analyze(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    if (tw_job_is_cancelling(&app->job)) {
        print_error("the previous analysis is still stopping. Try again in a moment.");
        return;
    }
    if (tw_job_is_running(&app->job)) {
        print_error("an analysis is already running. Use 'cancel' to stop it.");
        return;
    }

    tw_document snapshot;
    if (!take_snapshot(app, &snapshot))
        return;
    size_t lines = snapshot.count;

    tw_status status = tw_job_start(&app->job, &snapshot, &app->options, on_background_done, app);
    if (status != TW_OK) {
        print_error("cannot start the analysis: %s", tw_status_str(status));
        return;
    }
    printf("Analysing %zu lines in the background. Use 'status' or 'cancel'.\n", lines);
}

static void cmd_cancel(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    if (!tw_job_is_running(&app->job)) {
        puts("No analysis is running.");
        return;
    }
    tw_job_cancel(&app->job);
    puts("Cancelling the background analysis...");
}

static void cmd_status(tw_app *app, const char *arg, bool force)
{
    (void)arg;
    (void)force;
    pthread_mutex_lock(&app->session.lock);
    const char *path = app->session.path ? app->session.path : "(no file)";
    printf("File     : %s\n", path);
    printf("Lines    : %zu\n", app->session.doc.count);
    printf("Modified : %s\n", app->session.dirty ? "yes" : "no");
    pthread_mutex_unlock(&app->session.lock);

    printf("Folding  : %s\n", app->options.fold == TW_FOLD_TURKISH ? "turkish" : "default");
    if (tw_job_is_running(&app->job))
        printf("Analysis : running, %.0f%% done\n",
               100.0 * tw_analysis_progress_fraction(&app->job.progress));
    else
        puts("Analysis : idle");
}

static void cmd_help(tw_app *app, const char *arg, bool force)
{
    (void)app;
    (void)arg;
    (void)force;
    tw_command_print_help();
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
    const char *name;
    const char *usage;
    const char *summary;
    arg_kind args;
    bool forceable; /* accepts a trailing '!' */
    command_fn run; /* NULL for quit/exit, handled by the dispatcher */
} command_spec;

static const command_spec COMMANDS[] = {
    {"open", "open[!] FILE", "Load FILE (use open! to discard unsaved changes)", ARGS_FILE, true,
     cmd_open},
    {"append", "append TEXT", "Add TEXT as a new last line", ARGS_TEXT, false, cmd_append},
    {"show", "show", "Print the document with line numbers", ARGS_NONE, false, cmd_show},
    {"save", "save [FILE]", "Save atomically to FILE or to the current file", ARGS_OPTIONAL_FILE,
     false, cmd_save},
    {"analyze", "analyze", "Find near-duplicate lines in the background", ARGS_NONE, false,
     cmd_analyze},
    {"analyze-sync", "analyze-sync", "Find near-duplicate lines and wait for the result", ARGS_NONE,
     false, cmd_analyze_sync},
    {"cancel", "cancel", "Stop the running background analysis", ARGS_NONE, false, cmd_cancel},
    {"status", "status", "Show the file, unsaved changes and analysis progress", ARGS_NONE, false,
     cmd_status},
    {"help", "help", "Show this list", ARGS_NONE, false, cmd_help},
    {"quit", "quit[!]", "Exit (quit! discards unsaved changes)", ARGS_NONE, true, NULL},
    {"exit", "exit[!]", "Same as quit", ARGS_NONE, true, NULL},
};

#define COMMAND_COUNT (sizeof COMMANDS / sizeof COMMANDS[0])

void tw_command_print_help(void)
{
    puts("Commands:");
    for (size_t i = 0; i < COMMAND_COUNT; i++)
        printf("  %-16s %s\n", COMMANDS[i].usage, COMMANDS[i].summary);
}

static const command_spec *find_command(const char *name)
{
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(COMMANDS[i].name, name) == 0)
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

static bool validate_args(const command_spec *spec, char *arg)
{
    if (spec->args != ARGS_TEXT)
        trim_trailing_space(arg);

    switch (spec->args) {
    case ARGS_NONE:
        if (*arg != '\0') {
            print_error("'%s' takes no arguments.", spec->name);
            return false;
        }
        return true;
    case ARGS_FILE:
        if (*arg == '\0') {
            print_error("usage: %s", spec->usage);
            return false;
        }
        /* fall through */
    case ARGS_OPTIONAL_FILE:
        if (strpbrk(arg, " \t") != NULL) {
            print_error("file names with spaces are not supported yet.");
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
    puts("You have unsaved changes. Type 'quit' again to discard them, or 'save' first.");
    return TW_CMD_CONTINUE;
}

tw_cmd_result tw_command_execute(tw_app *app, char *line)
{
    char *name = line;
    while (*name == ' ' || *name == '\t')
        name++;
    if (*name == '\0')
        return TW_CMD_CONTINUE;

    char *arg = name;
    while (*arg != '\0' && *arg != ' ' && *arg != '\t')
        arg++;
    if (*arg != '\0')
        *arg++ = '\0';
    while (*arg == ' ' || *arg == '\t')
        arg++;

    bool force = false;
    size_t name_len = strlen(name);
    if (name_len > 1 && name[name_len - 1] == '!') {
        name[name_len - 1] = '\0';
        force = true;
    }

    const command_spec *spec = find_command(name);
    if (spec == NULL || (force && !spec->forceable)) {
        print_error("unknown command '%s%s'. Type 'help' for the list.", name, force ? "!" : "");
        return TW_CMD_CONTINUE;
    }

    bool quitting = spec->run == NULL;
    if (!quitting)
        app->quit_armed = false;

    if (!validate_args(spec, arg))
        return TW_CMD_CONTINUE;

    if (quitting)
        return request_quit(app, force);

    spec->run(app, arg, force);
    return TW_CMD_CONTINUE;
}
