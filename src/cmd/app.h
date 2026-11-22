/*
 * Application state shared by the command layer and the REPL.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_APP_H
#define TWINLINE_CMD_APP_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

#include "twinline/analysis.h"
#include "twinline/job.h"
#include "twinline/session.h"
#include "twinline/similarity.h"
#include "twinline/status.h"

#define TW_PROMPT "twinline> "

struct tw_app;

/*
 * Delivers text produced outside a command (for example the result of a
 * background analysis), possibly from a worker thread. The plain REPL prints
 * it and redraws the prompt; the terminal UI shows it above the input box.
 */
typedef void (*tw_emit_fn)(struct tw_app *app, const char *text, bool is_error);

/*
 * Optional: receives finished analysis results as data instead of text, so a
 * front end can present them its own way. May be called from a worker
 * thread. When NULL, results are printed as a text report.
 */
typedef void (*tw_result_fn)(struct tw_app *app, const tw_analysis_result *result);

/*
 * Optional: puts `text` into the input line for the user to change (used by
 * "edit"). Called on the main thread while a command runs. When NULL, "edit"
 * prints the line instead.
 */
typedef void (*tw_prefill_fn)(struct tw_app *app, const char *text);

/*
 * Optional: shows a short, unimportant notice (for example "Autosaved
 * notes.txt") without interrupting. May be called from any thread. When
 * NULL, notices are not shown.
 */
typedef void (*tw_notify_fn)(struct tw_app *app, const char *text);

/* Background services: autosave and file watching (cmd/services.c). */
typedef struct tw_services {
    pthread_t thread;
    bool running;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    bool stop;            /* guarded by lock */
    int inotify_fd;       /* -1 when not available */
    int wake_pipe[2];     /* wakes the thread out of poll(); -1 when not used */
    double last_autosave; /* CLOCK_MONOTONIC seconds */
} tw_services;

/* Around highlighted text in command output when tw_app.marks is set. */
#define TW_MARK_ON  "\x1e"
#define TW_MARK_OFF "\x1f"

typedef struct tw_app {
    FILE *out; /* where commands write results (stdout by default) */
    FILE *err; /* where commands write errors (stderr by default) */
    tw_emit_fn emit;
    tw_result_fn on_result;
    tw_prefill_fn prefill;
    tw_notify_fn notify;
    bool marks;         /* wrap highlights (find matches) in TW_MARK_ON / TW_MARK_OFF */
    void *ui;           /* owned by the active front end, may be NULL */
    tw_session session; /* holds the active buffer */

    /* Open buffers, numbered from 1 for the user. The active one lives in
     * `session`; its slot here is an empty placeholder. Changed and read
     * only with session.lock held. */
    tw_buffer *buffers;
    size_t buffer_count;
    size_t current;

    tw_analysis_job job;
    tw_analysis_options options;
    /* The last "clusters" result, for "dedupe" and "export". It belongs to
     * the document state and settings it was computed with. Main thread only. */
    tw_similarity clusters;
    bool has_clusters;
    unsigned long long clusters_state; /* undo state of the buffer */
    tw_analysis_options clusters_options;

    /* Services. Change the settings only while the services are stopped or
     * with services.lock held. */
    unsigned autosave_interval; /* seconds; 0 turns autosave off */
    bool watch;                 /* report changes made to the files by other programs */
    tw_services services;

    bool quit_armed;                    /* "quit" was typed once with unsaved changes */
    atomic_bool quitting;               /* suppresses the prompt redraw from the worker */
    atomic_bool sync_running;           /* analyze-sync is working */
    tw_analysis_progress sync_progress; /* its progress, for the UI */
} tw_app;

tw_status tw_app_init(tw_app *app);
void tw_app_destroy(tw_app *app);

/* Starts the autosave and file-watching thread (if either is enabled). */
void tw_app_start_services(tw_app *app);

/* Stops the thread; recovery files of every buffer are removed when
 * `clean_exit` (a normal quit) is set. */
void tw_app_stop_services(tw_app *app, bool clean_exit);

/* One pass of the services, as the thread does every second: reports files
 * changed on disk and, when `autosave` is set (or the interval is over),
 * writes recovery files. Exposed for tests. */
void tw_services_tick(tw_app *app, bool autosave);

/* Number of buffers with unsaved changes. Takes session.lock. */
size_t tw_app_dirty_buffers(tw_app *app);

/* tw_emit_fn for the plain REPL: prints the text, then the prompt again. */
void tw_app_emit_plain(tw_app *app, const char *text, bool is_error);

#endif /* TWINLINE_CMD_APP_H */
