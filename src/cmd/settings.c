/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/settings.h"

#include <stdlib.h>
#include <string.h>

const char *const TW_SETTING_NAMES[] = {"threshold", "threads", "strategy", "exhaustive",
                                        "fold",      "ngram",   NULL};

static bool parse_uint(const char *text, unsigned long max, unsigned long *out)
{
    if (*text < '0' || *text > '9')
        return false;
    unsigned long value = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9')
            return false;
        value = value * 10 + (unsigned long)(*p - '0');
        if (value > max)
            return false;
    }
    *out = value;
    return true;
}

static bool parse_switch(const char *text, bool *out)
{
    if (strcmp(text, "on") == 0 || strcmp(text, "yes") == 0 || strcmp(text, "true") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(text, "off") == 0 || strcmp(text, "no") == 0 || strcmp(text, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

bool tw_setting_set(tw_analysis_options *options, const char *name, const char *value,
                    char *message, size_t size)
{
    if (strcmp(name, "threshold") == 0) {
        char *end = NULL;
        double x = strtod(value, &end);
        if (end == value || *end != '\0' || !(x > 0.0 && x <= 1.0)) {
            snprintf(message, size, "threshold must be a number above 0 and at most 1, got '%s'.",
                     value);
            return false;
        }
        options->threshold = x;
    } else if (strcmp(name, "threads") == 0) {
        unsigned long n = 0;
        if (strcmp(value, "auto") == 0) {
            n = 0;
        } else if (!parse_uint(value, TW_MAX_THREADS, &n) || n == 0) {
            snprintf(message, size, "threads must be 'auto' or 1 to %d, got '%s'.", TW_MAX_THREADS,
                     value);
            return false;
        }
        options->threads = (size_t)n;
    } else if (strcmp(name, "strategy") == 0) {
        if (!tw_strategy_parse(value, &options->strategy)) {
            snprintf(message, size, "strategy must be jaccard, exact or minhash, got '%s'.", value);
            return false;
        }
    } else if (strcmp(name, "exhaustive") == 0) {
        if (!parse_switch(value, &options->exhaustive)) {
            snprintf(message, size, "exhaustive must be on or off, got '%s'.", value);
            return false;
        }
    } else if (strcmp(name, "fold") == 0) {
        if (strcmp(value, "default") == 0) {
            options->fold = TW_FOLD_DEFAULT;
        } else if (strcmp(value, "turkish") == 0) {
            options->fold = TW_FOLD_TURKISH;
        } else {
            snprintf(message, size, "fold must be default or turkish, got '%s'.", value);
            return false;
        }
    } else if (strcmp(name, "ngram") == 0) {
        unsigned long n = 0;
        if (!parse_uint(value, TW_NGRAM_MAX, &n) || n == 0) {
            snprintf(message, size, "ngram must be 1 to %d, got '%s'.", TW_NGRAM_MAX, value);
            return false;
        }
        options->ngram = (int)n;
    } else {
        snprintf(message, size,
                 "unknown setting '%s'. Settings: threshold, threads, strategy, exhaustive, "
                 "fold, ngram.",
                 name);
        return false;
    }
    return true;
}

void tw_settings_print(FILE *out, const tw_analysis_options *options)
{
    fprintf(out, "threshold   %.2f\n", options->threshold);
    if (options->threads == 0)
        fprintf(out, "threads     auto (one per processor)\n");
    else
        fprintf(out, "threads     %zu\n", options->threads);
    fprintf(out, "strategy    %s\n", tw_strategy_name(options->strategy));
    fprintf(out, "exhaustive  %s\n", options->exhaustive ? "on" : "off");
    fprintf(out, "fold        %s\n", options->fold == TW_FOLD_TURKISH ? "turkish" : "default");
    fprintf(out, "ngram       %d\n", options->ngram);
}
