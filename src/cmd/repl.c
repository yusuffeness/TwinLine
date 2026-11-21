/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd/commands.h"

static void strip_newline(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

void tw_repl_run(tw_app *app)
{
    char *line = NULL;
    size_t cap = 0;

    for (;;) {
        flockfile(stdout);
        fputs(TW_PROMPT, stdout);
        fflush(stdout);
        funlockfile(stdout);

        if (getline(&line, &cap, stdin) < 0) { /* end of input (Ctrl+D) */
            putchar('\n');
            pthread_mutex_lock(&app->session.lock);
            bool dirty = app->session.dirty;
            pthread_mutex_unlock(&app->session.lock);
            if (dirty)
                fputs("warning: input ended; unsaved changes were discarded.\n", stderr);
            break;
        }

        strip_newline(line);
        if (tw_command_execute(app, line) == TW_CMD_QUIT)
            break;
    }
    free(line);

    atomic_store(&app->quitting, true);
    if (tw_job_is_running(&app->job)) {
        puts("Stopping the background analysis...");
        tw_job_cancel(&app->job);
    }
    tw_job_wait(&app->job);
}
