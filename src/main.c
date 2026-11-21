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
#include "tui/ui.h"
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
    size_t threads;
    bool exhaustive;
    bool plain;
    bool no_color;
    const tw_theme *theme;
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
            "  --threads=N      Worker threads for analysis (default: one per processor)\n"
            "  --exhaustive     Compare every pair of lines (slower; adds the mean similarity)\n"
            "  --theme=NAME     Colour theme: dark, light, dracula or nord\n"
            "  --no-color       Do not use colours (also honours the NO_COLOR variable)\n"
            "  --plain          Use the plain line interface instead of the terminal UI\n"
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

/* Parses a thread count in [1, TW_MAX_THREADS]; returns 0 if invalid. */
static size_t parse_threads(const char *text)
{
    size_t value = 0;
    if (*text == '\0')
        return 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        value = value * 10 + (size_t)(*p - '0');
        if (value > TW_MAX_THREADS)
            return 0;
    }
    return value;
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
            } else if (strncmp(arg, "--threads=", 10) == 0) {
                opts->threads = parse_threads(arg + 10);
                if (opts->threads == 0)
                    return usage_error(prog, "thread count must be between 1 and 256, got", arg);
            } else if (strcmp(arg, "--exhaustive") == 0) {
                opts->exhaustive = true;
            } else if (strcmp(arg, "--plain") == 0) {
                opts->plain = true;
            } else if (strcmp(arg, "--no-color") == 0) {
                opts->no_color = true;
            } else if (strncmp(arg, "--theme=", 8) == 0) {
                opts->theme = tw_theme_find(arg + 8);
                if (opts->theme == NULL)
                    return usage_error(prog,
                                       "unknown theme (try dark, light, dracula, nord):", arg);
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
    cli_options opts = {NULL, TW_FOLD_DEFAULT, 0, false, false, false, NULL};

    int exit_code = parse_args(argc, argv, prog, &opts);
    if (exit_code >= 0)
        return exit_code;

    tw_app app;
    if (tw_app_init(&app) != TW_OK) {
        fprintf(stderr, "%s: cannot initialise\n", prog);
        return TW_EXIT_FAILURE;
    }
    app.options.fold = opts.fold;
    app.options.threads = opts.threads;
    app.options.exhaustive = opts.exhaustive;

    bool interactive = false;
    if (!opts.plain && tw_ui_available()) {
        tw_ui_options ui_options = {
            opts.theme, opts.no_color ? TW_COLOR_NONE : tw_color_mode_detect(), opts.file};
        interactive = tw_ui_run(&app, &ui_options);
    }
    if (!interactive) {
        printf("%s %s - type 'help' for the list of commands.\n", TWINLINE_NAME, TWINLINE_VERSION);
        if (opts.file != NULL)
            tw_command_open(&app, opts.file, false);
        tw_repl_run(&app);
    }
    tw_app_destroy(&app);
    return TW_EXIT_OK;
}
