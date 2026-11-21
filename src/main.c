/*
 * TwinLine - a concurrent terminal editor that finds the lines that look alike.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "cmd/app.h"
#include "cmd/commands.h"
#include "cmd/repl.h"
#include "twinline/version.h"

/* Exit codes shared by every TwinLine subcommand. */
enum {
    TW_EXIT_OK = 0,
    TW_EXIT_FAILURE = 1,
    TW_EXIT_USAGE = 2,
};

typedef struct cli_options {
    const char *file;
    tw_fold_mode fold;
} cli_options;

/* Returns the executable name without its directory, for messages. */
static const char *program_name(const char *argv0)
{
    if (argv0 == NULL || *argv0 == '\0')
        return "twinline";
    const char *slash = strrchr(argv0, '/');
    return slash != NULL ? slash + 1 : argv0;
}

static void print_version(FILE *out)
{
    fprintf(out, "%s %s\n", TWINLINE_NAME, TWINLINE_VERSION);
}

static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
            "%s %s - %s\n"
            "\n"
            "Usage:\n"
            "  %s [options] [FILE]\n"
            "\n"
            "Options:\n"
            "  --fold=MODE      Case folding for analysis: 'default' or 'turkish'\n"
            "  -h, --help       Show this help and exit\n"
            "  -V, --version    Show version information and exit\n"
            "\n"
            "Project page: %s\n",
            TWINLINE_NAME, TWINLINE_VERSION, TWINLINE_DESCRIPTION, prog, TWINLINE_HOMEPAGE);
}

static int usage_error(const char *prog, const char *message, const char *arg)
{
    fprintf(stderr, "%s: %s '%s'\n", prog, message, arg);
    fprintf(stderr, "Try '%s --help' for more information.\n", prog);
    return TW_EXIT_USAGE;
}

/* Returns -1 to continue, otherwise the exit code. */
static int parse_args(int argc, char **argv, const char *prog, cli_options *opts)
{
    bool only_files = false;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (!only_files && arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "--") == 0) {
                only_files = true;
            } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                print_usage(stdout, prog);
                return TW_EXIT_OK;
            } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
                print_version(stdout);
                return TW_EXIT_OK;
            } else if (strcmp(arg, "--fold=default") == 0) {
                opts->fold = TW_FOLD_DEFAULT;
            } else if (strcmp(arg, "--fold=turkish") == 0) {
                opts->fold = TW_FOLD_TURKISH;
            } else {
                return usage_error(prog, "unknown option", arg);
            }
            continue;
        }

        if (opts->file != NULL)
            return usage_error(prog, "only one file can be opened, got an extra", arg);
        opts->file = arg;
    }
    return -1;
}

int main(int argc, char **argv)
{
    const char *prog = program_name(argc > 0 ? argv[0] : NULL);
    cli_options opts = {NULL, TW_FOLD_DEFAULT};

    int exit_code = parse_args(argc, argv, prog, &opts);
    if (exit_code >= 0)
        return exit_code;

    tw_app app;
    if (tw_app_init(&app) != TW_OK) {
        fprintf(stderr, "%s: cannot initialise\n", prog);
        return TW_EXIT_FAILURE;
    }
    app.options.fold = opts.fold;

    printf("%s %s - type 'help' for the list of commands.\n", TWINLINE_NAME, TWINLINE_VERSION);
    if (opts.file != NULL)
        tw_command_open(&app, opts.file, false);

    tw_repl_run(&app);
    tw_app_destroy(&app);
    return TW_EXIT_OK;
}
