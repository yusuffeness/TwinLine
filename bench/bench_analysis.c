/*
 * bench_analysis - measures the near-duplicate analysis.
 *
 * For each input file, mode (pruned / exhaustive) and thread count, the
 * analysis is run several times and the median wall-clock time is reported,
 * together with the speed-up over one thread.
 *
 * Usage:
 *   bench_analysis [--threads=1,2,4,8] [--repeat=3] [--modes=pruned,exhaustive]
 *                  [--csv=FILE] [--json=FILE] CORPUS...
 *
 * The JSON output follows the "customSmallerIsBetter" format of
 * github-action-benchmark, so CI can track regressions.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "twinline/analysis.h"
#include "twinline/fileio.h"
#include "twinline/sysinfo.h"

#define MAX_THREAD_STEPS 32
#define MAX_REPEAT       50

typedef struct bench_config {
    size_t threads[MAX_THREAD_STEPS];
    size_t thread_steps;
    int repeat;
    bool run_pruned;
    bool run_exhaustive;
    const char *csv_path;
    const char *json_path;
} bench_config;

typedef struct measurement {
    const char *corpus;
    const char *mode;
    size_t threads;
    double seconds;
    double speedup;
    unsigned long long compared;
    unsigned long long pairs;
} measurement;

static int compare_double(const void *lhs, const void *rhs)
{
    double a = *(const double *)lhs;
    double b = *(const double *)rhs;
    return (a > b) - (a < b);
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static bool parse_thread_list(const char *text, bench_config *cfg)
{
    cfg->thread_steps = 0;
    const char *p = text;
    while (*p != '\0') {
        char *end = NULL;
        unsigned long value = strtoul(p, &end, 10);
        if (end == p || value == 0 || value > TW_MAX_THREADS ||
            cfg->thread_steps == MAX_THREAD_STEPS)
            return false;
        cfg->threads[cfg->thread_steps++] = (size_t)value;
        p = end;
        if (*p == ',')
            p++;
        else if (*p != '\0')
            return false;
    }
    return cfg->thread_steps > 0;
}

static void default_thread_list(bench_config *cfg)
{
    size_t cpus = tw_cpu_count();
    cfg->thread_steps = 0;
    for (size_t t = 1; t <= cpus && cfg->thread_steps < MAX_THREAD_STEPS; t *= 2)
        cfg->threads[cfg->thread_steps++] = t;
    if (cfg->threads[cfg->thread_steps - 1] != cpus && cfg->thread_steps < MAX_THREAD_STEPS)
        cfg->threads[cfg->thread_steps++] = cpus;
}

static void usage(void)
{
    fputs("usage: bench_analysis [--threads=1,2,4] [--repeat=N] [--modes=pruned,exhaustive]\n"
          "                      [--csv=FILE] [--json=FILE] CORPUS...\n",
          stderr);
}

static bool measure(const tw_document *doc, bool exhaustive, size_t threads, int repeat,
                    double *median, tw_analysis_result *last)
{
    double times[MAX_REPEAT];
    tw_analysis_options options;
    tw_analysis_options_default(&options);
    options.threads = threads;
    options.exhaustive = exhaustive;

    for (int r = 0; r < repeat; r++) {
        if (tw_analysis_run(doc, &options, NULL, NULL, last) != TW_OK)
            return false;
        times[r] = last->seconds;
    }
    qsort(times, (size_t)repeat, sizeof times[0], compare_double);
    *median = times[repeat / 2];
    return true;
}

static void write_csv(const char *path, const measurement *m, size_t count)
{
    FILE *out = fopen(path, "w");
    if (out == NULL) {
        perror(path);
        return;
    }
    fputs("corpus,mode,threads,seconds,speedup,compared,pairs\n", out);
    for (size_t i = 0; i < count; i++)
        fprintf(out, "%s,%s,%zu,%.6f,%.3f,%llu,%llu\n", m[i].corpus, m[i].mode, m[i].threads,
                m[i].seconds, m[i].speedup, m[i].compared, m[i].pairs);
    fclose(out);
}

static void write_json(const char *path, const measurement *m, size_t count)
{
    FILE *out = fopen(path, "w");
    if (out == NULL) {
        perror(path);
        return;
    }
    fputs("[\n", out);
    for (size_t i = 0; i < count; i++)
        fprintf(out,
                "  {\"name\": \"%s / %s / %zu thread%s\", \"unit\": \"s\", \"value\": %.6f}%s\n",
                m[i].corpus, m[i].mode, m[i].threads, m[i].threads == 1 ? "" : "s", m[i].seconds,
                i + 1 < count ? "," : "");
    fputs("]\n", out);
    fclose(out);
}

static int run_corpus(const char *path, const bench_config *cfg, measurement *results,
                      size_t *count)
{
    tw_document doc;
    int err_no = 0;
    if (tw_file_load(path, &doc, &err_no) != TW_OK) {
        fprintf(stderr, "cannot read %s: %s\n", path, strerror(err_no));
        return 1;
    }
    printf("\n%s: %zu lines\n", base_name(path), doc.count);
    printf("  %-10s %7s %10s %8s %14s\n", "mode", "threads", "seconds", "speedup", "compared");

    for (int mode = 0; mode < 2; mode++) {
        bool exhaustive = mode == 1;
        if ((exhaustive && !cfg->run_exhaustive) || (!exhaustive && !cfg->run_pruned))
            continue;

        double baseline = 0.0;
        for (size_t s = 0; s < cfg->thread_steps; s++) {
            double seconds = 0.0;
            tw_analysis_result r;
            if (!measure(&doc, exhaustive, cfg->threads[s], cfg->repeat, &seconds, &r)) {
                fprintf(stderr, "analysis failed\n");
                tw_document_free(&doc);
                return 1;
            }
            if (s == 0)
                baseline = seconds * (double)cfg->threads[0];

            measurement *m = &results[(*count)++];
            m->corpus = base_name(path);
            m->mode = exhaustive ? "exhaustive" : "pruned";
            m->threads = cfg->threads[s];
            m->seconds = seconds;
            m->speedup = seconds > 0.0 ? baseline / seconds : 0.0;
            m->compared = r.compared;
            m->pairs = r.pairs;
            printf("  %-10s %7zu %10.3f %7.2fx %14llu\n", m->mode, m->threads, m->seconds,
                   m->speedup, m->compared);
            fflush(stdout);
        }
    }
    tw_document_free(&doc);
    return 0;
}

int main(int argc, char **argv)
{
    bench_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.repeat = 3;
    cfg.run_pruned = true;
    cfg.run_exhaustive = true;
    default_thread_list(&cfg);

    int first_file = argc;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strncmp(arg, "--threads=", 10) == 0) {
            if (!parse_thread_list(arg + 10, &cfg)) {
                usage();
                return 2;
            }
        } else if (strncmp(arg, "--repeat=", 9) == 0) {
            cfg.repeat = atoi(arg + 9);
            if (cfg.repeat < 1 || cfg.repeat > MAX_REPEAT) {
                usage();
                return 2;
            }
        } else if (strncmp(arg, "--modes=", 8) == 0) {
            cfg.run_pruned = strstr(arg + 8, "pruned") != NULL;
            cfg.run_exhaustive = strstr(arg + 8, "exhaustive") != NULL;
        } else if (strncmp(arg, "--csv=", 6) == 0) {
            cfg.csv_path = arg + 6;
        } else if (strncmp(arg, "--json=", 7) == 0) {
            cfg.json_path = arg + 7;
        } else if (arg[0] == '-') {
            usage();
            return 2;
        } else {
            first_file = i;
            break;
        }
    }
    if (first_file >= argc || (!cfg.run_pruned && !cfg.run_exhaustive)) {
        usage();
        return 2;
    }

    size_t files = (size_t)(argc - first_file);
    measurement *results = calloc(files * 2 * cfg.thread_steps, sizeof *results);
    if (results == NULL)
        return 1;

    printf("TwinLine analysis benchmark: %zu processor%s, median of %d run%s\n", tw_cpu_count(),
           tw_cpu_count() == 1 ? "" : "s", cfg.repeat, cfg.repeat == 1 ? "" : "s");

    size_t count = 0;
    int status = 0;
    for (int i = first_file; i < argc && status == 0; i++)
        status = run_corpus(argv[i], &cfg, results, &count);

    if (status == 0 && cfg.csv_path != NULL)
        write_csv(cfg.csv_path, results, count);
    if (status == 0 && cfg.json_path != NULL)
        write_json(cfg.json_path, results, count);

    free(results);
    return status;
}
