/*
 * Scripting mode: `twinline SUBCOMMAND [options] FILE`.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_CLI_H
#define TWINLINE_CMD_CLI_H

#include <stdbool.h>

#include "cmd/config.h"
#include "twinline/analysis.h"

/* Exit codes, shared by the interactive and the scripting mode. */
enum {
    TW_EXIT_OK = 0,      /* success; analyze and clusters: nothing similar found */
    TW_EXIT_FOUND = 1,   /* analyze and clusters: similar lines were found */
    TW_EXIT_USAGE = 2,   /* bad command line or configuration file */
    TW_EXIT_IO = 3,      /* a file could not be read or written */
    TW_EXIT_FAILURE = 4, /* anything else (out of memory, interrupted) */
};

/* True if `name` is a subcommand (analyze, clusters, dedupe, export, config). */
bool tw_cli_is_subcommand(const char *name);

/*
 * Runs the subcommand argv[0] with its arguments; `config` holds the
 * settings from the configuration file. Returns the exit code.
 */
int tw_cli_run(int argc, char **argv, const tw_config *config, const char *config_path,
               bool config_found, const char *prog);

/*
 * Handles one analysis option at argv[*i] (--threshold=X, --strategy=NAME,
 * --exact, --minhash, --ngram=N, --threads=N, -t N, --exhaustive,
 * --fold=MODE), advancing *i past a separate value. Returns 1 if handled,
 * 0 if argv[*i] is not an analysis option, or -1 after printing an error.
 */
int tw_cli_analysis_option(int argc, char **argv, int *i, tw_analysis_options *options,
                           const char *prog);

#endif /* TWINLINE_CMD_CLI_H */
