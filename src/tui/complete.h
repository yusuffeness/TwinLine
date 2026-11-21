/*
 * Fuzzy matching for the command menu and file name completion.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_TUI_COMPLETE_H
#define TWINLINE_TUI_COMPLETE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "twinline/status.h"

/*
 * Matches `pattern` as a case-insensitive subsequence of `text`. On success
 * stores a score (higher is better: consecutive letters and word starts
 * count more) and a bit mask of the matched byte positions (first 64 bytes).
 * An empty pattern matches everything with score 0.
 */
bool tw_fuzzy_match(const char *pattern, const char *text, int *score, uint64_t *bits);

typedef struct tw_completions {
    char **items; /* sorted; directories end with '/' */
    size_t count;
} tw_completions;

/*
 * Lists the paths that start with `prefix` ("notes/dr" -> "notes/draft.txt",
 * "notes/drafts/"). Hidden entries are listed only when the last component
 * of `prefix` starts with '.'.
 */
tw_status tw_complete_path(const char *prefix, tw_completions *out);
void tw_completions_free(tw_completions *completions);

/* Length of the longest prefix shared by every item (0 when empty). */
size_t tw_completions_common_prefix(const tw_completions *completions);

#endif /* TWINLINE_TUI_COMPLETE_H */
