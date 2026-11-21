/*
 * Shared declarations for the fuzz targets.
 *
 * Each target defines LLVMFuzzerTestOneInput(). With Clang the targets are
 * linked against libFuzzer (-fsanitize=fuzzer); with other compilers they
 * are linked against standalone_main.c, which replays files from the seed
 * corpus so the targets still run as regression tests.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TESTS_FUZZ_H
#define TWINLINE_TESTS_FUZZ_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* Returns a NUL-terminated heap copy of the input (embedded NULs truncate). */
static inline char *tw_fuzz_cstring(const uint8_t *data, size_t size)
{
    char *s = malloc(size + 1);
    if (s == NULL)
        return NULL;
    if (size > 0)
        memcpy(s, data, size);
    s[size] = '\0';
    return s;
}

/* Aborts (which the fuzzer reports as a crash) when an invariant breaks. */
#define TW_FUZZ_ASSERT(cond)                                                                       \
    do {                                                                                           \
        if (!(cond))                                                                               \
            abort();                                                                               \
    } while (0)

#endif /* TWINLINE_TESTS_FUZZ_H */
