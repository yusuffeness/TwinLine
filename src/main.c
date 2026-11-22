/*
 * TwinLine - a concurrent terminal editor that finds the lines that look alike.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd/app.h"
#include "cmd/cli.h"
#include "cmd/commands.h"
#include "cmd/config.h"
#include "cmd/repl.h"
#include "cmd/settings.h"
#include "tui/ui.h"
#include "twinline/version.h"

typedef struct cli_options {
    const char *file;
    tw_analysis_options analysis;
    bool plain;
    bool no_color;
    bool no_animation;
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
            "  %s [options] [FILE]          edit FILE interactively\n"
            "  %s analyze [options] FILE    report similar lines (exit 1 if any)\n"
            "  %s clusters [options] FILE   list clusters of similar lines\n"
            "  %s dedupe [options] FILE     remove duplicate lines (-o OUT, --in-place)\n"
            "  %s export [options] FILE OUT write a .json, .csv or .html report\n"
            "  %s config [--path]           show the configuration in effect\n"
            "FILE may be '-' for standard input in the subcommands; each one has --help.\n"
            "\n"
            "Options:\n"
            "  --fold=MODE      Case folding for analysis: 'default' or 'turkish'\n"
            "  --threads=N      Worker threads for analysis (default: one per processor)\n"
            "  --exhaustive     Compare every pair of lines (slower; adds the mean similarity)\n"
            "  --threshold=X    Near-duplicate threshold, above 0 and at most 1 (default 0.70)\n"
            "  --strategy=NAME  jaccard (exact, default), exact (identical lines only) or\n"
            "                   minhash (fast for large files; may miss a few pairs)\n"
            "  --exact, --minhash  Short for --strategy=exact and --strategy=minhash\n"
            "  --ngram=N        Longest word n-gram compared, 1 to 4 (default 2)\n"
            "  --theme=NAME     Colour theme: dark, light, dracula or nord\n"
            "  --no-color       Do not use colours (also honours the NO_COLOR variable)\n"
            "  --no-animation   Turn off animations (also TWINLINE_REDUCE_MOTION=1)\n"
            "  --plain          Use the plain line interface instead of the terminal UI\n"
            "  --config=FILE    Read settings from FILE instead of the default config.toml\n"
            "  --no-config      Do not read a configuration file\n"
            "  --json           (analyze, clusters) Print JSON\n"
            "  -h, --help       Show this help and exit\n"
            "  -V, --version    Show version information and exit\n"
            "\n"
            "Project page: %s\n",
            TWINLINE_NAME, TWINLINE_VERSION, TWINLINE_DESCRIPTION, prog, prog, prog, prog, prog,
            prog, TWINLINE_HOMEPAGE);
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
            } else if (strncmp(arg, "--threads=", 10) == 0) {
                /* The interactive mode keeps its long-standing message. */
                opts->analysis.threads = parse_threads(arg + 10);
                if (opts->analysis.threads == 0)
                    return usage_error(prog, "thread count must be between 1 and 256, got", arg);
            } else if (strncmp(arg, "--config=", 9) == 0 || strcmp(arg, "--no-config") == 0) {
                /* already applied before the arguments were parsed */
            } else if (strcmp(arg, "--plain") == 0) {
                opts->plain = true;
            } else if (strcmp(arg, "--no-color") == 0) {
                opts->no_color = true;
            } else if (strcmp(arg, "--no-animation") == 0) {
                opts->no_animation = true;
            } else if (strncmp(arg, "--theme=", 8) == 0) {
                opts->theme = tw_theme_find(arg + 8);
                if (opts->theme == NULL)
                    return usage_error(prog,
                                       "unknown theme (try dark, light, dracula, nord):", arg);
            } else {
                int handled = tw_cli_analysis_option(argc, argv, &i, &opts->analysis, prog);
                if (handled < 0)
                    return TW_EXIT_USAGE;
                if (handled == 0)
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

/* Finds --config=FILE and --no-config before the other arguments are parsed. */
static void config_flags(int argc, char **argv, const char **path, bool *disabled)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0)
            break;
        if (strncmp(argv[i], "--config=", 9) == 0)
            *path = argv[i] + 9;
        else if (strcmp(argv[i], "--no-config") == 0)
            *disabled = true;
    }
}

int main(int argc, char **argv)
{
    const char *prog = program_name(argc > 0 ? argv[0] : NULL);

    /* Settings: built-in defaults, then the configuration file, then flags. */
    const char *config_arg = NULL;
    bool no_config = false;
    config_flags(argc, argv, &config_arg, &no_config);
    char *config_path = NULL;
    if (!no_config)
        config_path = config_arg != NULL ? strdup(config_arg) : tw_config_default_path();
    tw_config config;
    tw_config_default(&config);
    bool config_found = false;
    char message[512];
    if (config_path != NULL &&
        !tw_config_load(config_path, &config, &config_found, message, sizeof message)) {
        fprintf(stderr, "%s: %s\n", prog, message);
        free(config_path);
        return TW_EXIT_USAGE;
    }
    if (config_arg != NULL && !config_found) {
        fprintf(stderr, "%s: configuration file '%s' does not exist\n", prog, config_arg);
        free(config_path);
        return TW_EXIT_USAGE;
    }

    if (argc > 1 && tw_cli_is_subcommand(argv[1])) {
        int code = tw_cli_run(argc - 1, argv + 1, &config, config_path, config_found, prog);
        free(config_path);
        return code;
    }

    cli_options opts;
    memset(&opts, 0, sizeof opts);
    opts.analysis = config.analysis;
    opts.theme = tw_theme_find(config.theme);
    opts.no_color = !config.color;
    opts.no_animation = !config.animations;

    int exit_code = parse_args(argc, argv, prog, &opts);
    if (exit_code >= 0) {
        free(config_path);
        return exit_code;
    }

    tw_app app;
    if (tw_app_init(&app) != TW_OK) {
        fprintf(stderr, "%s: cannot initialise\n", prog);
        free(config_path);
        return TW_EXIT_FAILURE;
    }
    app.options = opts.analysis;
    app.autosave_interval = config.autosave ? config.autosave_interval : 0;
    app.watch = config.watch;
    tw_app_start_services(&app);

    bool interactive = false;
    if (!opts.plain && tw_ui_available()) {
        const char *reduce = getenv("TWINLINE_REDUCE_MOTION"); // NOLINT(concurrency-mt-unsafe)
        bool reduce_motion = reduce != NULL && reduce[0] != '\0' && strcmp(reduce, "0") != 0;
        tw_ui_options ui_options = {opts.theme,
                                    opts.no_color ? TW_COLOR_NONE : tw_color_mode_detect(),
                                    opts.file, !opts.no_animation && !reduce_motion};
        interactive = tw_ui_run(&app, &ui_options);
    }
    if (!interactive) {
        printf("%s %s - type 'help' for the list of commands.\n", TWINLINE_NAME, TWINLINE_VERSION);
        if (opts.file != NULL)
            tw_command_open(&app, opts.file, false);
        tw_repl_run(&app);
    }
    tw_app_destroy(&app);
    free(config_path);
    return TW_EXIT_OK;
}
