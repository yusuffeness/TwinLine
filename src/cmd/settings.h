/*
 * Analysis settings by name, shared by the command line and "set".
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_SETTINGS_H
#define TWINLINE_CMD_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "twinline/analysis.h"

/*
 * Sets `name` to `value`:
 *   threshold  0 < x <= 1            near-duplicate threshold
 *   threads    1..256, or auto       worker threads
 *   strategy   jaccard|exact|minhash how pairs are found
 *   exhaustive on|off                jaccard: score every pair
 *   fold       default|turkish       case folding
 *   ngram      1..4                  longest word n-gram in the features
 * On failure returns false and leaves a message (without "error: ") in
 * `message`.
 */
bool tw_setting_set(tw_analysis_options *options, const char *name, const char *value,
                    char *message, size_t size);

/* Prints every setting as "name value" lines. */
void tw_settings_print(FILE *out, const tw_analysis_options *options);

/* Names of the settings, NULL-terminated (for completion and help). */
extern const char *const TW_SETTING_NAMES[];

#endif /* TWINLINE_CMD_SETTINGS_H */
