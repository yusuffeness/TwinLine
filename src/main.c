/*
 * TwinLine - a concurrent terminal editor that finds the lines that look alike.
 *
 * Entry point. At this stage (v0.1.0) the program only exposes the command
 * line surface; the editor, the analysis engine and the terminal UI are added
 * in later milestones (see docs/ROADMAP.md).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "twinline/version.h"

/* Exit codes shared by every TwinLine subcommand. */
enum {
    TW_EXIT_OK = 0,
    TW_EXIT_USAGE = 2,
};

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
            "  %s [options]\n"
            "\n"
            "Options:\n"
            "  -h, --help       Show this help and exit\n"
            "  -V, --version    Show version information and exit\n"
            "\n"
            "Project page: %s\n",
            TWINLINE_NAME, TWINLINE_VERSION, TWINLINE_DESCRIPTION, prog, TWINLINE_HOMEPAGE);
}

int main(int argc, char **argv)
{
    const char *prog = program_name(argc > 0 ? argv[0] : NULL);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout, prog);
            return TW_EXIT_OK;
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version(stdout);
            return TW_EXIT_OK;
        }

        fprintf(stderr, "%s: unknown option '%s'\n", prog, arg);
        fprintf(stderr, "Try '%s --help' for more information.\n", prog);
        return TW_EXIT_USAGE;
    }

    print_version(stdout);
    puts("Work in progress: the interactive editor arrives in a later release.");
    puts("Run with --help to see the available options.");
    return TW_EXIT_OK;
}
