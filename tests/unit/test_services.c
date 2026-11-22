/*
 * Unit tests for src/cmd/services.c (autosave, file watching) and the
 * recovery and reload commands built on them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../tw_test.h"
#include "cmd/app.h"
#include "cmd/commands.h"
#include "twinline/recovery.h"

static char dir[64];
static char file[128];
static char last_message[1024];
static int messages;
static char last_notice[256];

static void capture_emit(tw_app *app, const char *text, bool is_error)
{
    (void)app;
    (void)is_error;
    snprintf(last_message, sizeof last_message, "%s", text);
    messages++;
}

static void capture_notify(tw_app *app, const char *text)
{
    (void)app;
    snprintf(last_notice, sizeof last_notice, "%s", text);
}

static void write_file(const char *text)
{
    FILE *f = fopen(file, "w");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

/* Runs a command with its output thrown away. */
static void run(tw_app *app, const char *command)
{
    char line[256];
    snprintf(line, sizeof line, "%s", command);
    tw_command_execute(app, line);
}

TW_TEST(autosave_writes_and_save_removes_the_recovery_file)
{
    tw_app app;
    TW_CHECK_EQ_INT(tw_app_init(&app), TW_OK);
    FILE *sink = fopen("/dev/null", "w");
    app.out = sink;
    app.err = sink;
    app.emit = capture_emit;
    app.notify = capture_notify;

    write_file("first\n");
    char command[256];
    snprintf(command, sizeof command, "open %s", file);
    run(&app, command);

    /* Nothing unsaved: nothing to write. */
    tw_services_tick(&app, true);
    TW_CHECK(!tw_recovery_pending(file));

    run(&app, "append second");
    last_notice[0] = '\0';
    tw_services_tick(&app, true);
    TW_CHECK(tw_recovery_pending(file));
    TW_CHECK(strncmp(last_notice, "Autosaved ", 10) == 0);
    tw_document doc;
    int err = 0;
    TW_CHECK_EQ_INT(tw_recovery_read(file, &doc, NULL, &err), TW_OK);
    TW_CHECK_EQ_SIZE(doc.count, 2);
    tw_document_free(&doc);

    /* The same state is not written twice. */
    last_notice[0] = '\0';
    tw_services_tick(&app, true);
    TW_CHECK_EQ_STR(last_notice, "");

    run(&app, "save");
    TW_CHECK(!tw_recovery_pending(file));

    fclose(sink);
    app.out = stdout;
    app.err = stderr;
    tw_app_destroy(&app);
}

TW_TEST(changes_on_disk_are_reported_once)
{
    tw_app app;
    TW_CHECK_EQ_INT(tw_app_init(&app), TW_OK);
    FILE *sink = fopen("/dev/null", "w");
    app.out = sink;
    app.err = sink;
    app.emit = capture_emit;
    app.watch = true;

    write_file("one\n");
    char command[256];
    snprintf(command, sizeof command, "open %s", file);
    run(&app, command);

    messages = 0;
    tw_services_tick(&app, false);
    TW_CHECK_EQ_INT(messages, 0); /* unchanged */

    write_file("one\ntwo, written by someone else\n");
    tw_services_tick(&app, false);
    TW_CHECK_EQ_INT(messages, 1);
    TW_CHECK(strstr(last_message, "was changed by another program") != NULL);
    TW_CHECK(strstr(last_message, "'reload'") != NULL);
    tw_services_tick(&app, false);
    TW_CHECK_EQ_INT(messages, 1); /* reported once */

    run(&app, "reload");
    pthread_mutex_lock(&app.session.lock);
    TW_CHECK_EQ_SIZE(app.session.doc.count, 2);
    TW_CHECK(!app.session.dirty);
    pthread_mutex_unlock(&app.session.lock);
    tw_services_tick(&app, false);
    TW_CHECK_EQ_INT(messages, 1); /* reloading is not a change */

    run(&app, "save"); /* our own save is not a change either */
    tw_services_tick(&app, false);
    TW_CHECK_EQ_INT(messages, 1);

    unlink(file);
    tw_services_tick(&app, false);
    TW_CHECK_EQ_INT(messages, 2);
    TW_CHECK(strstr(last_message, "was deleted or moved") != NULL);

    fclose(sink);
    app.out = stdout;
    app.err = stderr;
    tw_app_destroy(&app);
}

TW_TEST(recover_loads_the_changes_as_one_edit)
{
    write_file("on disk\n");
    tw_document doc;
    tw_document_init(&doc);
    TW_CHECK_EQ_INT(tw_document_append(&doc, "on disk"), TW_OK);
    TW_CHECK_EQ_INT(tw_document_append(&doc, "lost in a crash"), TW_OK);
    int err = 0;
    TW_CHECK_EQ_INT(tw_recovery_write(file, &doc, &err), TW_OK);
    tw_document_free(&doc);

    tw_app app;
    TW_CHECK_EQ_INT(tw_app_init(&app), TW_OK);
    FILE *sink = fopen("/dev/null", "w");
    app.out = sink;
    app.err = sink;
    app.autosave_interval = 1;
    char command[256];
    snprintf(command, sizeof command, "open %s", file);
    run(&app, command);
    TW_CHECK(app.session.recovery_offered);

    /* While the old file waits for a decision, autosave leaves it alone. */
    run(&app, "append newer edit");
    tw_services_tick(&app, true);
    TW_CHECK_EQ_INT(tw_recovery_read(file, &doc, NULL, &err), TW_OK);
    TW_CHECK_EQ_STR(doc.lines[1], "lost in a crash");
    tw_document_free(&doc);

    run(&app, "recover");
    TW_CHECK_EQ_SIZE(app.session.doc.count, 2);
    TW_CHECK_EQ_STR(app.session.doc.lines[1], "lost in a crash");
    TW_CHECK(app.session.dirty);
    run(&app, "undo");
    TW_CHECK_EQ_STR(app.session.doc.lines[1], "newer edit");

    run(&app, "recover discard");
    TW_CHECK(!tw_recovery_pending(file));

    fclose(sink);
    app.out = stdout;
    app.err = stderr;
    tw_app_destroy(&app);
}

TW_TEST(thread_starts_and_stops)
{
    tw_app app;
    TW_CHECK_EQ_INT(tw_app_init(&app), TW_OK);
    app.autosave_interval = 1;
    app.watch = true;
    tw_app_start_services(&app);
    TW_CHECK(app.services.running);
    tw_app_start_services(&app); /* already running: no second thread */
    struct timespec pause = {0, 50 * 1000 * 1000};
    nanosleep(&pause, NULL);
    tw_app_stop_services(&app, false);
    TW_CHECK(!app.services.running);
    tw_app_stop_services(&app, false); /* stopping twice is harmless */

    /* Nothing enabled: no thread. */
    app.autosave_interval = 0;
    app.watch = false;
    tw_app_start_services(&app);
    TW_CHECK(!app.services.running);
    tw_app_destroy(&app);
}

int main(void)
{
    puts("services");
    snprintf(dir, sizeof dir, "/tmp/twinline-services-XXXXXX");
    if (mkdtemp(dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(file, sizeof file, "%s/notes.txt", dir);

    TW_RUN(autosave_writes_and_save_removes_the_recovery_file);
    TW_RUN(changes_on_disk_are_reported_once);
    TW_RUN(recover_loads_the_changes_as_one_edit);
    TW_RUN(thread_starts_and_stops);

    unlink(file);
    tw_recovery_remove(file);
    rmdir(dir);
    return tw_test_summary();
}
