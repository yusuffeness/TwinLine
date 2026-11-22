/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd/export.h"
#include "cmd/report.h"
#include "cmd/settings.h"
#include "twinline/fileio.h"
#include "twinline/similarity.h"
#include "twinline/version.h"

static const char *const SUBCOMMANDS[] = {"analyze", "clusters", "dedupe", "export", "config"};

bool tw_cli_is_subcommand(const char *name)
{
    for (size_t i = 0; i < sizeof SUBCOMMANDS / sizeof SUBCOMMANDS[0]; i++) {
        if (strcmp(name, SUBCOMMANDS[i]) == 0)
            return true;
    }
    return false;
}

static int usage_error(const char *prog, const char *command, const char *message, const char *arg)
{
    if (arg != NULL)
        fprintf(stderr, "%s %s: %s '%s'\n", prog, command, message, arg);
    else
        fprintf(stderr, "%s %s: %s\n", prog, command, message);
    fprintf(stderr, "Try '%s %s --help' for more information.\n", prog, command);
    return TW_EXIT_USAGE;
}

/* ---------------------------------------------------------------- options */

static bool apply(tw_analysis_options *o, const char *name, const char *value, const char *prog)
{
    char message[200];
    if (tw_setting_set(o, name, value, message, sizeof message))
        return true;
    fprintf(stderr, "%s: %s\n", prog, message);
    fprintf(stderr, "Try '%s --help' for more information.\n", prog);
    return false;
}

int tw_cli_analysis_option(int argc, char **argv, int *i, tw_analysis_options *o, const char *prog)
{
    const char *arg = argv[*i];
    static const char *const VALUED[][2] = {
        {"--threshold=", "threshold"}, {"--strategy=", "strategy"}, {"--ngram=", "ngram"},
        {"--threads=", "threads"},     {"--fold=", "fold"},
    };
    for (size_t k = 0; k < sizeof VALUED / sizeof VALUED[0]; k++) {
        size_t len = strlen(VALUED[k][0]);
        if (strncmp(arg, VALUED[k][0], len) == 0) {
            /* --threads keeps its own message and rejects "auto" here. */
            if (strcmp(VALUED[k][1], "threads") == 0 && strcmp(arg + len, "auto") == 0)
                return apply(o, "threads", "auto", prog) ? 1 : -1;
            return apply(o, VALUED[k][1], arg + len, prog) ? 1 : -1;
        }
    }
    if (strcmp(arg, "-t") == 0) {
        if (*i + 1 >= argc) {
            fprintf(stderr, "%s: -t needs a number of threads\n", prog);
            fprintf(stderr, "Try '%s --help' for more information.\n", prog);
            return -1;
        }
        *i += 1;
        return apply(o, "threads", argv[*i], prog) ? 1 : -1;
    }
    if (strcmp(arg, "--exhaustive") == 0) {
        o->exhaustive = true;
        return 1;
    }
    if (strcmp(arg, "--exact") == 0) {
        o->strategy = TW_STRATEGY_EXACT;
        return 1;
    }
    if (strcmp(arg, "--minhash") == 0) {
        o->strategy = TW_STRATEGY_MINHASH;
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------- the input */

/* Loads FILE, or standard input for "-". Returns an exit code. */
static int load_input(const char *prog, const char *command, const char *path, tw_document *doc)
{
    int err_no = 0;
    tw_status status;
    if (strcmp(path, "-") == 0)
        status = tw_file_read(stdin, doc, &err_no);
    else
        status = tw_file_load(path, doc, &err_no);
    if (status == TW_OK)
        return TW_EXIT_OK;
    fprintf(stderr, "%s %s: cannot read '%s': %s\n", prog, command, path,
            status == TW_ERR_IO && err_no != 0 ? strerror(err_no) // NOLINT(concurrency-mt-unsafe)
                                               : tw_status_str(status));
    return status == TW_ERR_IO ? TW_EXIT_IO : TW_EXIT_FAILURE;
}

static const char *display_name(const char *path)
{
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/* -------------------------------------------------------------- the work */

typedef struct cli_args {
    const char *command;
    tw_analysis_options options;
    const char *input;
    const char *second; /* export: the report file */
    const char *output; /* dedupe: -o */
    bool in_place;
    bool json;
    bool path_only; /* config --path */
    bool quiet;
    bool help;
} cli_args;

static void print_help(const char *prog, const char *command)
{
    if (strcmp(command, "analyze") == 0)
        printf("Usage: %s analyze [OPTIONS] FILE\n\n"
               "Reports identical and near-duplicate lines of FILE ('-' reads standard input).\n"
               "Exits with 1 if any were found, 0 if none.\n\n"
               "  --json           Print the result as JSON\n",
               prog);
    else if (strcmp(command, "clusters") == 0)
        printf("Usage: %s clusters [OPTIONS] FILE\n\n"
               "Lists the clusters of similar lines of FILE ('-' reads standard input).\n"
               "Exits with 1 if any were found, 0 if none.\n\n"
               "  --json           Print the clusters and pairs as JSON\n",
               prog);
    else if (strcmp(command, "dedupe") == 0)
        printf("Usage: %s dedupe [OPTIONS] FILE [-o OUTPUT | --in-place]\n\n"
               "Keeps the first line of every cluster of similar lines and removes the others.\n"
               "Writes the result to standard output, to OUTPUT, or over FILE.\n\n"
               "  -o, --output=FILE  Write the result to FILE (atomically)\n"
               "  -i, --in-place     Replace FILE (atomically)\n"
               "  -q, --quiet        Do not print the summary on standard error\n",
               prog);
    else if (strcmp(command, "export") == 0)
        printf("Usage: %s export [OPTIONS] FILE REPORT\n\n"
               "Writes the clusters of FILE to REPORT, a .json, .csv or .html file.\n",
               prog);
    else
        printf("Usage: %s config [--path]\n\n"
               "Prints the configuration in effect as a TOML file, or with --path only the\n"
               "file it is read from.\n",
               prog);
    if (strcmp(command, "config") != 0)
        printf("\nAnalysis options:\n"
               "  --threshold=X    Near-duplicate threshold, above 0 and at most 1\n"
               "  --strategy=NAME  jaccard, exact or minhash (also --exact, --minhash)\n"
               "  --ngram=N        Longest word n-gram compared, 1 to 4\n"
               "  -t, --threads=N  Worker threads, 1 to 256 or auto\n"
               "  --exhaustive     jaccard: compare every pair (adds the mean similarity)\n"
               "  --fold=MODE      Case folding: default or turkish\n");
}

static int parse_cli(int argc, char **argv, const tw_config *config, const char *prog, cli_args *a)
{
    memset(a, 0, sizeof *a);
    a->command = argv[0];
    a->options = config->analysis;
    bool only_files = false;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!only_files && arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "--") == 0) {
                only_files = true;
                continue;
            }
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                a->help = true;
                continue;
            }
            if (strncmp(arg, "--config=", 9) == 0 || strcmp(arg, "--no-config") == 0)
                continue; /* handled before the subcommand */
            int handled = tw_cli_analysis_option(argc, argv, &i, &a->options, prog);
            if (handled < 0)
                return TW_EXIT_USAGE;
            if (handled > 0)
                continue;
            if (strcmp(arg, "--json") == 0) {
                a->json = true;
            } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
                a->quiet = true;
            } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--in-place") == 0) {
                a->in_place = true;
            } else if (strcmp(arg, "-o") == 0) {
                if (i + 1 >= argc)
                    return usage_error(prog, a->command, "-o needs a file name", NULL);
                a->output = argv[++i];
            } else if (strncmp(arg, "--output=", 9) == 0) {
                a->output = arg + 9;
            } else if (strcmp(arg, "--path") == 0 && strcmp(a->command, "config") == 0) {
                a->path_only = true;
            } else {
                return usage_error(prog, a->command, "unknown option", arg);
            }
            continue;
        }
        if (a->input == NULL)
            a->input = arg;
        else if (a->second == NULL && strcmp(a->command, "export") == 0)
            a->second = arg;
        else
            return usage_error(prog, a->command, "unexpected extra argument", arg);
    }
    if (a->help || strcmp(a->command, "config") == 0)
        return -1;
    if (a->input == NULL)
        return usage_error(prog, a->command, "no input file (use '-' for standard input)", NULL);
    if (strcmp(a->command, "export") == 0 && a->second == NULL)
        return usage_error(prog, a->command, "no report file given", NULL);
    if (strcmp(a->command, "dedupe") != 0 && (a->output != NULL || a->in_place))
        return usage_error(prog, a->command, "-o and --in-place only apply to dedupe", NULL);
    if (a->in_place && (a->output != NULL || strcmp(a->input, "-") == 0))
        return usage_error(prog, a->command,
                           "--in-place needs a file and cannot be combined with -o", NULL);
    if (a->json && (strcmp(a->command, "dedupe") == 0 || strcmp(a->command, "export") == 0))
        return usage_error(prog, a->command, "--json applies to analyze and clusters", NULL);
    return -1;
}

static int status_exit(tw_status status)
{
    return status == TW_ERR_IO ? TW_EXIT_IO : TW_EXIT_FAILURE;
}

static int run_analyze(const cli_args *a, const tw_document *doc, const char *prog)
{
    tw_analysis_result result;
    tw_status status = tw_analysis_run(doc, &a->options, NULL, NULL, &result);
    if (status != TW_OK) {
        fprintf(stderr, "%s analyze: %s\n", prog, tw_status_str(status));
        return status_exit(status);
    }
    const char *name = display_name(a->input);
    if (a->json) {
        tw_export_result_json(stdout, &result, doc, &a->options,
                              strcmp(a->input, "-") == 0 ? NULL : a->input);
    } else {
        char title[600];
        snprintf(title, sizeof title, "Analysis of %s:", name);
        tw_report_print(stdout, title, &result);
    }
    return result.identical + result.near > 0 ? TW_EXIT_FOUND : TW_EXIT_OK;
}

static int run_with_clusters(const cli_args *a, const tw_document *doc, const char *prog,
                             tw_similarity *s)
{
    tw_status status = tw_similarity_run(doc, &a->options, NULL, NULL, s);
    if (status != TW_OK) {
        fprintf(stderr, "%s %s: %s\n", prog, a->command, tw_status_str(status));
        return status_exit(status);
    }
    return TW_EXIT_OK;
}

static int run_clusters(const cli_args *a, const tw_document *doc, const char *prog)
{
    tw_similarity s;
    int code = run_with_clusters(a, doc, prog, &s);
    if (code != TW_EXIT_OK)
        return code;
    const char *source = strcmp(a->input, "-") == 0 ? NULL : a->input;
    if (a->json)
        tw_export_write(stdout, TW_EXPORT_JSON, doc, &s, &a->options, source);
    else
        tw_export_write_text(stdout, doc, &s, &a->options, display_name(a->input));
    code = s.cluster_count > 0 ? TW_EXIT_FOUND : TW_EXIT_OK;
    tw_similarity_free(&s);
    return code;
}

static int write_document(const char *prog, const char *path, const tw_document *doc)
{
    int err_no = 0;
    tw_status status = tw_file_save(path, doc, &err_no);
    if (status == TW_OK)
        return TW_EXIT_OK;
    fprintf(stderr, "%s dedupe: cannot write '%s': %s\n", prog, path,
            status == TW_ERR_IO && err_no != 0 ? strerror(err_no) // NOLINT(concurrency-mt-unsafe)
                                               : tw_status_str(status));
    return status_exit(status);
}

static int run_dedupe(const cli_args *a, tw_document *doc, const char *prog)
{
    tw_similarity s;
    int code = run_with_clusters(a, doc, prog, &s);
    if (code != TW_EXIT_OK)
        return code;

    /* Keep the first line of every cluster, drop the rest. */
    bool *drop = calloc(doc->count ? doc->count : 1, sizeof *drop);
    tw_document kept;
    tw_document_init(&kept);
    if (drop == NULL) {
        tw_similarity_free(&s);
        fprintf(stderr, "%s dedupe: %s\n", prog, tw_status_str(TW_ERR_NOMEM));
        return TW_EXIT_FAILURE;
    }
    size_t removed = 0;
    for (size_t k = 0; k < s.cluster_count; k++) {
        for (size_t i = s.cluster_start[k] + 1; i < s.cluster_start[k + 1]; i++) {
            drop[s.cluster_lines[i]] = true;
            removed++;
        }
    }
    /* `kept` borrows the lines: only its array is freed below. */
    tw_status status = TW_OK;
    for (size_t i = 0; i < doc->count && status == TW_OK; i++) {
        if (!drop[i])
            status = tw_document_push(&kept, doc->lines[i]);
    }
    free(drop);
    size_t clusters = s.cluster_count;
    tw_similarity_free(&s);
    if (status != TW_OK) {
        free(kept.lines);
        fprintf(stderr, "%s dedupe: %s\n", prog, tw_status_str(status));
        return TW_EXIT_FAILURE;
    }

    if (a->in_place || a->output != NULL) {
        code = write_document(prog, a->in_place ? a->input : a->output, &kept);
    } else {
        for (size_t i = 0; i < kept.count; i++)
            printf("%s\n", kept.lines[i]);
        code = ferror(stdout) ? TW_EXIT_IO : TW_EXIT_OK;
    }
    free(kept.lines);
    if (code == TW_EXIT_OK && !a->quiet)
        fprintf(stderr, "Removed %zu line%s from %zu cluster%s; %zu line%s kept.\n", removed,
                removed == 1 ? "" : "s", clusters, clusters == 1 ? "" : "s", doc->count - removed,
                doc->count - removed == 1 ? "" : "s");
    return code;
}

static int run_export(const cli_args *a, const tw_document *doc, const char *prog)
{
    tw_export_format format;
    if (a->second == NULL)
        return usage_error(prog, "export", "no report file given", NULL);
    if (!tw_export_format_for(a->second, &format))
        return usage_error(prog, "export",
                           "the report must end in .json, .csv or .html:", a->second);
    tw_similarity s;
    int code = run_with_clusters(a, doc, prog, &s);
    if (code != TW_EXIT_OK)
        return code;
    FILE *out = fopen(a->second, "w");
    if (out == NULL) {
        fprintf(stderr, "%s export: cannot write '%s': %s\n", prog, a->second,
                strerror(errno)); // NOLINT(concurrency-mt-unsafe)
        tw_similarity_free(&s);
        return TW_EXIT_IO;
    }
    bool ok = tw_export_write(out, format, doc, &s, &a->options,
                              strcmp(a->input, "-") == 0 ? NULL : a->input);
    if (fclose(out) != 0)
        ok = false;
    tw_similarity_free(&s);
    if (!ok) {
        fprintf(stderr, "%s export: cannot write '%s'\n", prog, a->second);
        return TW_EXIT_IO;
    }
    return TW_EXIT_OK;
}

int tw_cli_run(int argc, char **argv, const tw_config *config, const char *config_path,
               bool config_found, const char *prog)
{
    cli_args a;
    int code = parse_cli(argc, argv, config, prog, &a);
    if (code >= 0)
        return code;
    if (a.help) {
        print_help(prog, a.command);
        return TW_EXIT_OK;
    }

    if (strcmp(a.command, "config") == 0) {
        if (a.path_only) {
            printf("%s\n", config_path != NULL ? config_path : "(no configuration file)");
            return TW_EXIT_OK;
        }
        printf("# TwinLine %s configuration\n", TWINLINE_VERSION);
        if (config_path == NULL)
            printf("# No configuration file is used.\n");
        else if (config_found)
            printf("# Read from %s\n", config_path);
        else
            printf("# %s does not exist: these are the defaults.\n", config_path);
        printf("\n");
        tw_config_print(stdout, config);
        return TW_EXIT_OK;
    }

    if (a.input == NULL || (strcmp(a.command, "export") == 0 && a.second == NULL))
        return TW_EXIT_USAGE; /* parse_cli() has already said why */
    tw_document doc;
    code = load_input(prog, a.command, a.input, &doc);
    if (code != TW_EXIT_OK)
        return code;
    if (strcmp(a.command, "analyze") == 0)
        code = run_analyze(&a, &doc, prog);
    else if (strcmp(a.command, "clusters") == 0)
        code = run_clusters(&a, &doc, prog);
    else if (strcmp(a.command, "dedupe") == 0)
        code = run_dedupe(&a, &doc, prog);
    else
        code = run_export(&a, &doc, prog);
    tw_document_free(&doc);
    if (fflush(stdout) != 0 && code < TW_EXIT_USAGE)
        code = TW_EXIT_IO;
    return code;
}
