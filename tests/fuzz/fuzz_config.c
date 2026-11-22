/*
 * Fuzz target: the config.toml reader.
 *
 * Any text either parses or is rejected with a "name:line: problem"
 * message; whatever it does, every setting stays within its valid range.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cmd/config.h"
#include "fuzz.h"
#include "tui/theme.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *text = tw_fuzz_cstring(data, size);
    if (text == NULL)
        return 0;

    tw_config config;
    tw_config_default(&config);
    char message[512] = "";
    bool ok = tw_config_parse(text, "fuzz.toml", &config, message, sizeof message);
    if (!ok)
        TW_FUZZ_ASSERT(strncmp(message, "fuzz.toml:", 10) == 0);

    const tw_analysis_options *a = &config.analysis;
    TW_FUZZ_ASSERT(a->threshold > 0.0 && a->threshold <= 1.0);
    TW_FUZZ_ASSERT(a->threads <= TW_MAX_THREADS);
    TW_FUZZ_ASSERT(a->ngram >= 1 && a->ngram <= TW_NGRAM_MAX);
    TW_FUZZ_ASSERT(a->strategy == TW_STRATEGY_JACCARD || a->strategy == TW_STRATEGY_EXACT ||
                   a->strategy == TW_STRATEGY_MINHASH);
    TW_FUZZ_ASSERT(tw_theme_find(config.theme) != NULL);
    TW_FUZZ_ASSERT(config.autosave_interval >= 1 &&
                   config.autosave_interval <= TW_AUTOSAVE_MAX_INTERVAL);

    free(text);
    return 0;
}
