/*
 * Background services: autosave (recovery files) and file watching.
 *
 * One thread wakes every second, or at once when the operating system
 * reports a change in a watched folder (inotify on Linux). It then:
 *   - compares each buffer's file with what TwinLine last read or wrote and
 *     reports a change made by another program, once per change;
 *   - when the autosave interval is over, writes a recovery file for every
 *     buffer with a file name and unsaved changes newer than its last one.
 * Documents are copied under the session lock and written without it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cmd/app.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/inotify.h>
#define TW_HAVE_INOTIFY 1
#endif

#include "twinline/recovery.h"

#define TICK_MS 1000

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* A buffer slot: the session (the active buffer) or one of app->buffers. */
typedef struct slot_view {
    tw_document *doc;
    char *path;
    tw_undo *undo;
    tw_file_id *disk;
    bool *disk_reported;
    unsigned long long *autosaved_state;
    bool dirty;
    bool recovery_offered;
} slot_view;

/* Views of every buffer; returns the count. Caller holds session.lock. */
static size_t view_slots(tw_app *app, slot_view *views, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < app->buffer_count && n < max; i++) {
        slot_view *v = &views[n++];
        if (i == app->current) {
            tw_session *s = &app->session;
            *v = (slot_view){.doc = &s->doc,
                             .path = s->path,
                             .dirty = s->dirty,
                             .undo = &s->undo,
                             .disk = &s->disk,
                             .disk_reported = &s->disk_reported,
                             .autosaved_state = &s->autosaved_state,
                             .recovery_offered = s->recovery_offered};
        } else {
            tw_buffer *b = &app->buffers[i];
            *v = (slot_view){.doc = &b->doc,
                             .path = b->path,
                             .dirty = b->dirty,
                             .undo = &b->undo,
                             .disk = &b->disk,
                             .disk_reported = &b->disk_reported,
                             .autosaved_state = &b->autosaved_state,
                             .recovery_offered = b->recovery_offered};
        }
    }
    return n;
}

#define MAX_SLOTS 64 /* buffers looked at per tick */

/* ---------------------------------------------------------------- watch */

#ifdef TW_HAVE_INOTIFY
/* Watches the folder of every buffer's file (adding a watch twice is fine);
 * watching the folder also catches editors that save by renaming. */
static void update_watches(tw_app *app, const slot_view *views, size_t count)
{
    if (app->services.inotify_fd < 0)
        return;
    for (size_t i = 0; i < count; i++) {
        if (views[i].path == NULL)
            continue;
        char dir[4096];
        const char *slash = strrchr(views[i].path, '/');
        if (slash == NULL)
            snprintf(dir, sizeof dir, ".");
        else if (slash == views[i].path)
            snprintf(dir, sizeof dir, "/");
        else
            snprintf(dir, sizeof dir, "%.*s", (int)(slash - views[i].path), views[i].path);
        (void)inotify_add_watch(app->services.inotify_fd, dir,
                                IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE |
                                    IN_CREATE | IN_ATTRIB);
    }
}
#endif

static void check_files(tw_app *app)
{
    char messages[4][600];
    size_t message_count = 0;

    pthread_mutex_lock(&app->session.lock);
    slot_view views[MAX_SLOTS];
    size_t count = view_slots(app, views, MAX_SLOTS);
#ifdef TW_HAVE_INOTIFY
    update_watches(app, views, count);
#endif
    for (size_t i = 0; i < count; i++) {
        slot_view *v = &views[i];
        if (v->path == NULL || !v->disk->valid)
            continue; /* no file, or the file did not exist when opened */
        tw_file_id now;
        tw_file_id_read(v->path, &now);
        if (tw_file_id_equal(&now, v->disk)) {
            *v->disk_reported = false;
            continue;
        }
        if (*v->disk_reported || message_count == sizeof messages / sizeof messages[0])
            continue;
        *v->disk_reported = true;
        if (!now.valid)
            snprintf(messages[message_count++], sizeof messages[0],
                     "warning: '%s' was deleted or moved by another program. Type 'save' to "
                     "write it again.\n",
                     v->path);
        else
            snprintf(messages[message_count++], sizeof messages[0],
                     "warning: '%s' was changed by another program. Type 'reload%s' to load "
                     "it%s.\n",
                     v->path, v->dirty ? "!" : "",
                     v->dirty ? " (your unsaved changes would be lost)" : "");
    }
    pthread_mutex_unlock(&app->session.lock);

    for (size_t i = 0; i < message_count; i++)
        app->emit(app, messages[i], true);
}

/* -------------------------------------------------------------- autosave */

static void autosave(tw_app *app)
{
    typedef struct pending {
        char *path;
        tw_document copy;
        unsigned long long state;
    } pending;
    pending work[MAX_SLOTS];
    size_t count = 0;

    pthread_mutex_lock(&app->session.lock);
    slot_view views[MAX_SLOTS];
    size_t slots = view_slots(app, views, MAX_SLOTS);
    for (size_t i = 0; i < slots; i++) {
        slot_view *v = &views[i];
        unsigned long long state = tw_undo_state(v->undo);
        if (v->path == NULL || !v->dirty || state == *v->autosaved_state || v->recovery_offered)
            continue; /* never write over a recovery file the user has not looked at */
        pending *p = &work[count];
        p->path = strdup(v->path);
        if (p->path == NULL)
            continue;
        if (tw_document_clone(v->doc, &p->copy) != TW_OK) {
            free(p->path);
            continue;
        }
        p->state = state;
        count++;
    }
    pthread_mutex_unlock(&app->session.lock);

    size_t written = 0;
    const char *last = NULL;
    for (size_t i = 0; i < count; i++) {
        int err_no = 0;
        bool ok = tw_recovery_write(work[i].path, &work[i].copy, &err_no) == TW_OK;
        tw_document_free(&work[i].copy);
        if (!ok) {
            work[i].state = 0;
            continue;
        }
        written++;
        last = work[i].path;
    }

    /* Mark the buffers whose state was written (state ids are unique). */
    pthread_mutex_lock(&app->session.lock);
    slots = view_slots(app, views, MAX_SLOTS);
    for (size_t i = 0; i < count; i++) {
        for (size_t k = 0; k < slots && work[i].state != 0; k++) {
            if (tw_undo_state(views[k].undo) == work[i].state)
                *views[k].autosaved_state = work[i].state;
        }
    }
    pthread_mutex_unlock(&app->session.lock);

    if (written > 0 && app->notify != NULL) {
        char text[600];
        if (written == 1)
            snprintf(text, sizeof text, "Autosaved %s", last);
        else
            snprintf(text, sizeof text, "Autosaved %zu files", written);
        app->notify(app, text);
    }
    for (size_t i = 0; i < count; i++)
        free(work[i].path);
}

void tw_services_tick(tw_app *app, bool force_autosave)
{
    if (app->watch)
        check_files(app);
    double now = now_seconds();
    bool due = app->autosave_interval > 0 &&
               now - app->services.last_autosave >= (double)app->autosave_interval;
    if (force_autosave || due) {
        app->services.last_autosave = now;
        autosave(app);
    }
}

/* ---------------------------------------------------------------- thread */

static void *services_main(void *arg)
{
    tw_app *app = arg;
    tw_services *sv = &app->services;
    sv->last_autosave = now_seconds();
    for (;;) {
        pthread_mutex_lock(&sv->lock);
        bool stop = sv->stop;
        pthread_mutex_unlock(&sv->lock);
        if (stop)
            break;

        if (sv->wake_pipe[0] >= 0) {
            /* Sleep until a second has passed, a watched folder changed or
             * the thread is asked to stop. */
            struct pollfd fds[2] = {{sv->wake_pipe[0], POLLIN, 0}, {sv->inotify_fd, POLLIN, 0}};
            nfds_t n = sv->inotify_fd >= 0 ? 2 : 1;
            if (poll(fds, n, TICK_MS) > 0) {
                char drain[4096];
                if (fds[0].revents & POLLIN)
                    (void)read(sv->wake_pipe[0], drain, sizeof drain);
                if (n == 2 && (fds[1].revents & POLLIN))
                    (void)read(sv->inotify_fd, drain, sizeof drain);
            }
        } else {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += 1;
            pthread_mutex_lock(&sv->lock);
            if (!sv->stop)
                pthread_cond_timedwait(&sv->wake, &sv->lock, &until);
            pthread_mutex_unlock(&sv->lock);
        }

        pthread_mutex_lock(&sv->lock);
        stop = sv->stop;
        pthread_mutex_unlock(&sv->lock);
        if (!stop)
            tw_services_tick(app, false);
    }
    return NULL;
}

void tw_app_start_services(tw_app *app)
{
    tw_services *sv = &app->services;
    if (sv->running || (app->autosave_interval == 0 && !app->watch))
        return;
    if (pthread_mutex_init(&sv->lock, NULL) != 0)
        return;
    if (pthread_cond_init(&sv->wake, NULL) != 0) {
        pthread_mutex_destroy(&sv->lock);
        return;
    }
    sv->stop = false;
    if (pipe(sv->wake_pipe) != 0) {
        sv->wake_pipe[0] = -1;
        sv->wake_pipe[1] = -1;
    } else {
        (void)fcntl(sv->wake_pipe[0], F_SETFL, O_NONBLOCK);
        (void)fcntl(sv->wake_pipe[0], F_SETFD, FD_CLOEXEC);
        (void)fcntl(sv->wake_pipe[1], F_SETFD, FD_CLOEXEC);
    }
#ifdef TW_HAVE_INOTIFY
    if (app->watch && sv->wake_pipe[0] >= 0)
        sv->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
#endif
    sv->running = pthread_create(&sv->thread, NULL, services_main, app) == 0;
    if (!sv->running) {
        if (sv->inotify_fd >= 0)
            close(sv->inotify_fd);
        if (sv->wake_pipe[0] >= 0) {
            close(sv->wake_pipe[0]);
            close(sv->wake_pipe[1]);
        }
        sv->inotify_fd = -1;
        sv->wake_pipe[0] = -1;
        sv->wake_pipe[1] = -1;
        pthread_cond_destroy(&sv->wake);
        pthread_mutex_destroy(&sv->lock);
    }
}

static void remove_recovery_files(tw_app *app)
{
    pthread_mutex_lock(&app->session.lock);
    slot_view views[MAX_SLOTS];
    size_t count = view_slots(app, views, MAX_SLOTS);
    for (size_t i = 0; i < count; i++) {
        if (views[i].path != NULL && *views[i].autosaved_state != 0) {
            tw_recovery_remove(views[i].path);
            *views[i].autosaved_state = 0;
        }
    }
    pthread_mutex_unlock(&app->session.lock);
}

void tw_app_stop_services(tw_app *app, bool clean_exit)
{
    tw_services *sv = &app->services;
    if (sv->running) {
        pthread_mutex_lock(&sv->lock);
        sv->stop = true;
        pthread_cond_signal(&sv->wake);
        pthread_mutex_unlock(&sv->lock);
        if (sv->wake_pipe[1] >= 0)
            (void)write(sv->wake_pipe[1], "x", 1);
        pthread_join(sv->thread, NULL);
        sv->running = false;
        if (sv->inotify_fd >= 0)
            close(sv->inotify_fd);
        if (sv->wake_pipe[0] >= 0) {
            close(sv->wake_pipe[0]);
            close(sv->wake_pipe[1]);
        }
        sv->inotify_fd = -1;
        sv->wake_pipe[0] = -1;
        sv->wake_pipe[1] = -1;
        pthread_cond_destroy(&sv->wake);
        pthread_mutex_destroy(&sv->lock);
    }
    if (clean_exit && app->buffers != NULL)
        remove_recovery_files(app);
}
