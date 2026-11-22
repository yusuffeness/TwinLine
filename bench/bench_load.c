/*
 * bench_load - compares the two ways of loading a file: reading it line by
 * line, and large-file mode (a private memory mapping split in place).
 *
 * Usage:
 *   bench_load [--repeat=5] FILE...
 *
 * For each file the median wall-clock time of both loaders is printed,
 * together with the speed-up of large-file mode.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "twinline/fileio.h"

#define MAX_REPEAT 50

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int compare_doubles(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Median load time in seconds, or a negative value on error. */
static double measure(const char *path, size_t threshold, int repeat, size_t *lines)
{
    double times[MAX_REPEAT];
    for (int r = 0; r < repeat; r++) {
        tw_document doc;
        int err_no = 0;
        double start = now_seconds();
        tw_status status = tw_file_load_ex(path, &doc, &err_no, threshold);
        double elapsed = now_seconds() - start;
        if (status != TW_OK) {
            fprintf(stderr, "bench_load: cannot load '%s': %s\n", path,
                    err_no != 0 ? strerror(err_no) : tw_status_str(status)); // NOLINT
            return -1.0;
        }
        *lines = doc.count;
        tw_document_free(&doc); /* freeing is part of the cost of a document */
        times[r] = elapsed;
    }
    qsort(times, (size_t)repeat, sizeof times[0], compare_doubles);
    return times[repeat / 2];
}

int main(int argc, char **argv)
{
    int repeat = 5;
    int first_file = 1;
    for (; first_file < argc && strncmp(argv[first_file], "--", 2) == 0; first_file++) {
        if (strncmp(argv[first_file], "--repeat=", 9) == 0) {
            repeat = atoi(argv[first_file] + 9);
            if (repeat < 1 || repeat > MAX_REPEAT) {
                fprintf(stderr, "bench_load: --repeat must be 1-%d\n", MAX_REPEAT);
                return 2;
            }
        } else {
            fprintf(stderr, "usage: bench_load [--repeat=N] FILE...\n");
            return 2;
        }
    }
    if (first_file == argc) {
        fprintf(stderr, "usage: bench_load [--repeat=N] FILE...\n");
        return 2;
    }

    printf("%-28s %12s %12s %12s %9s\n", "file", "lines", "read (s)", "mmap (s)", "speed-up");
    for (int i = first_file; i < argc; i++) {
        size_t lines = 0;
        double read_time = measure(argv[i], SIZE_MAX, repeat, &lines);
        double map_time = measure(argv[i], 0, repeat, &lines);
        if (read_time < 0.0 || map_time < 0.0)
            return 1;
        printf("%-28s %12zu %12.4f %12.4f %8.2fx\n", argv[i], lines, read_time, map_time,
               read_time / map_time);
    }
    return 0;
}
