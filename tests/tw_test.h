/*
 * tw_test.h - a tiny, dependency-free unit test harness for TwinLine.
 *
 * Usage:
 *
 *     TW_TEST(test_something)
 *     {
 *         TW_CHECK(1 + 1 == 2);
 *         TW_CHECK_EQ_SIZE(strlen("abc"), 3);
 *     }
 *
 *     int main(void)
 *     {
 *         TW_RUN(test_something);
 *         return tw_test_summary();
 *     }
 *
 * A failed check prints its location and ends the current test; the other
 * tests still run. The process exits with status 1 if any test failed.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TESTS_TW_TEST_H
#define TWINLINE_TESTS_TW_TEST_H

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct tw_test_state {
    int run;
    int failed;
    bool current_failed;
} tw_test_state;

static tw_test_state tw_test_global = {0, 0, false};

static inline void tw_test_fail_location(const char *file, int line)
{
    if (!tw_test_global.current_failed)
        fputs("FAIL\n", stdout);
    tw_test_global.current_failed = true;
    printf("    %s:%d: ", file, line);
}

static inline void tw_test_run(const char *name, void (*fn)(void))
{
    printf("  %-48s ", name);
    fflush(stdout);
    tw_test_global.current_failed = false;
    tw_test_global.run++;
    fn();
    if (tw_test_global.current_failed)
        tw_test_global.failed++;
    else
        puts("ok");
}

static inline int tw_test_summary(void)
{
    printf("\n%d test(s), %d failed\n", tw_test_global.run, tw_test_global.failed);
    return tw_test_global.failed == 0 ? 0 : 1;
}

#define TW_TEST(name) static void name(void)
#define TW_RUN(name)  tw_test_run(#name, name)

#define TW_CHECK(cond)                                                                             \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            tw_test_fail_location(__FILE__, __LINE__);                                             \
            printf("check failed: %s\n", #cond);                                                   \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define TW_CHECK_EQ_INT(actual, expected)                                                          \
    do {                                                                                           \
        long long tw_a_ = (long long)(actual);                                                     \
        long long tw_e_ = (long long)(expected);                                                   \
        if (tw_a_ != tw_e_) {                                                                      \
            tw_test_fail_location(__FILE__, __LINE__);                                             \
            printf("%s == %lld, expected %lld\n", #actual, tw_a_, tw_e_);                          \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define TW_CHECK_EQ_SIZE(actual, expected)                                                         \
    do {                                                                                           \
        unsigned long long tw_a_ = (unsigned long long)(actual);                                   \
        unsigned long long tw_e_ = (unsigned long long)(expected);                                 \
        if (tw_a_ != tw_e_) {                                                                      \
            tw_test_fail_location(__FILE__, __LINE__);                                             \
            printf("%s == %llu, expected %llu\n", #actual, tw_a_, tw_e_);                          \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define TW_CHECK_EQ_STR(actual, expected)                                                          \
    do {                                                                                           \
        const char *tw_a_ = (actual);                                                              \
        const char *tw_e_ = (expected);                                                            \
        if (tw_a_ == NULL || tw_e_ == NULL || strcmp(tw_a_, tw_e_) != 0) {                         \
            tw_test_fail_location(__FILE__, __LINE__);                                             \
            printf("%s == \"%s\", expected \"%s\"\n", #actual, tw_a_ ? tw_a_ : "(null)",           \
                   tw_e_ ? tw_e_ : "(null)");                                                      \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define TW_CHECK_NEAR(actual, expected, tolerance)                                                 \
    do {                                                                                           \
        double tw_a_ = (double)(actual);                                                           \
        double tw_e_ = (double)(expected);                                                         \
        if (fabs(tw_a_ - tw_e_) > (tolerance)) {                                                   \
            tw_test_fail_location(__FILE__, __LINE__);                                             \
            printf("%s == %.6f, expected %.6f\n", #actual, tw_a_, tw_e_);                          \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#endif /* TWINLINE_TESTS_TW_TEST_H */
