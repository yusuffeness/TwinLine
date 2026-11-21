/*
 * Fuzz target: building feature sets and comparing them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fuzz.h"
#include "twinline/featureset.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *text = tw_fuzz_cstring(data, size);
    if (text == NULL)
        return 0;

    tw_featureset sets[2];
    for (int mode = TW_FOLD_DEFAULT; mode <= TW_FOLD_TURKISH; mode++) {
        size_t words = 0;
        tw_featureset *set = &sets[mode];
        if (tw_featureset_build(text, (tw_fold_mode)mode, set, &words) != TW_OK) {
            free(text);
            return 0;
        }

        /* Sorted, unique, and at most two features per word. */
        for (size_t i = 1; i < set->count; i++)
            TW_FUZZ_ASSERT(set->hashes[i - 1] < set->hashes[i]);
        TW_FUZZ_ASSERT(set->count <= 2 * words);
        TW_FUZZ_ASSERT((set->count == 0) == (words == 0));

        /* A non-empty set is identical to itself. */
        if (set->count > 0)
            TW_FUZZ_ASSERT(tw_featureset_jaccard(set, set) == 1.0);
    }

    double j = tw_featureset_jaccard(&sets[0], &sets[1]);
    TW_FUZZ_ASSERT(j >= 0.0 && j <= 1.0);
    TW_FUZZ_ASSERT(tw_featureset_intersection(&sets[0], &sets[1]) ==
                   tw_featureset_intersection(&sets[1], &sets[0]));

    tw_featureset_free(&sets[0]);
    tw_featureset_free(&sets[1]);
    free(text);
    return 0;
}
