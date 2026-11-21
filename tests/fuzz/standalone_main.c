/*
 * Replays fuzz inputs without libFuzzer.
 *
 * Usage: fuzz_target FILE...
 * Every file is passed once to LLVMFuzzerTestOneInput(). This lets the fuzz
 * targets run as ordinary regression tests with any compiler.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "fuzz.h"

static int run_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return 1;
    }

    unsigned char *data = NULL;
    size_t size = 0;
    size_t cap = 0;
    for (;;) {
        if (size == cap) {
            size_t new_cap = cap ? cap * 2 : 4096;
            unsigned char *grown = realloc(data, new_cap);
            if (grown == NULL) {
                free(data);
                fclose(file);
                return 1;
            }
            data = grown;
            cap = new_cap;
        }
        size_t got = fread(data + size, 1, cap - size, file);
        size += got;
        if (got == 0)
            break;
    }
    fclose(file);

    LLVMFuzzerTestOneInput(data, size);
    free(data);
    return 0;
}

int main(int argc, char **argv)
{
    int failures = 0;
    for (int i = 1; i < argc; i++)
        failures += run_file(argv[i]);
    printf("replayed %d input(s)\n", argc - 1 - failures);
    return failures == 0 ? 0 : 1;
}
