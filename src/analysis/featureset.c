/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/featureset.h"

#include <stdlib.h>
#include <string.h>

#define FNV64_OFFSET 0xcbf29ce484222325ULL
#define FNV64_PRIME  0x100000001b3ULL

/* Combines two word hashes into an order-sensitive bigram hash
 * (the finaliser of SplitMix64). */
static uint64_t mix_pair(uint64_t prev, uint64_t cur)
{
    uint64_t x = (prev * 0x9E3779B97F4A7C15ULL) ^ cur;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

static int compare_u64(const void *lhs, const void *rhs)
{
    uint64_t a = *(const uint64_t *)lhs;
    uint64_t b = *(const uint64_t *)rhs;
    return (a > b) - (a < b);
}

void tw_featureset_builder_init(tw_featureset_builder *builder)
{
    builder->buf = NULL;
    builder->cap = 0;
}

void tw_featureset_builder_free(tw_featureset_builder *builder)
{
    free(builder->buf);
    tw_featureset_builder_init(builder);
}

static tw_status builder_push(tw_featureset_builder *builder, size_t *count, uint64_t value)
{
    if (*count == builder->cap) {
        size_t new_cap = builder->cap ? builder->cap * 2 : 64;
        uint64_t *buf = realloc(builder->buf, new_cap * sizeof *buf);
        if (buf == NULL)
            return TW_ERR_NOMEM;
        builder->buf = buf;
        builder->cap = new_cap;
    }
    builder->buf[(*count)++] = value;
    return TW_OK;
}

static void hash_codepoint(uint64_t *hash, uint32_t cp)
{
    unsigned char bytes[4];
    size_t len = tw_utf8_encode(cp, bytes);
    for (size_t i = 0; i < len; i++) {
        *hash ^= bytes[i];
        *hash *= FNV64_PRIME;
    }
}

tw_status tw_featureset_builder_build(tw_featureset_builder *builder, const char *line,
                                      tw_fold_mode fold, tw_featureset *view, size_t *word_count)
{
    return tw_featureset_builder_build_ngram(builder, line, fold, TW_NGRAM_DEFAULT, view,
                                             word_count);
}

/* Order-k n-grams (k >= 3) extend the (k-1)-gram hash; the xor keeps them
 * apart from pairs that happen to start with the same value. */
static uint64_t extend_gram(uint64_t gram, uint64_t word, int k)
{
    return mix_pair(gram ^ (uint64_t)k, word);
}

tw_status tw_featureset_builder_build_ngram(tw_featureset_builder *builder, const char *line,
                                            tw_fold_mode fold, int ngram, tw_featureset *view,
                                            size_t *word_count)
{
    if (ngram < 1)
        ngram = 1;
    if (ngram > TW_NGRAM_MAX)
        ngram = TW_NGRAM_MAX;
    const unsigned char *p = (const unsigned char *)line;
    uint64_t recent[TW_NGRAM_MAX] = {0}; /* hashes of the last words, newest first */
    size_t words = 0;
    size_t count = 0;

    view->hashes = NULL;
    view->count = 0;

    while (*p != '\0') {
        uint32_t cp;
        size_t len = tw_utf8_decode(p, &cp);
        if (!tw_utf8_is_word(cp)) {
            p += len;
            continue;
        }

        /* Hash the folded word (FNV-1a over its UTF-8 bytes). */
        uint64_t hash = FNV64_OFFSET;
        for (;;) {
            hash_codepoint(&hash, tw_utf8_fold(cp, fold));
            p += len;
            if (*p == '\0')
                break;
            len = tw_utf8_decode(p, &cp);
            if (!tw_utf8_is_word(cp))
                break;
        }

        if (builder_push(builder, &count, hash) != TW_OK)
            return TW_ERR_NOMEM;
        /* The n-grams that end at this word, for n = 2 .. ngram. */
        for (int k = 2; k <= ngram && (size_t)(k - 1) <= words; k++) {
            uint64_t gram;
            if (k == 2) {
                gram = mix_pair(recent[0], hash); /* the pair hash used since v0.2 */
            } else {
                uint64_t head = recent[k - 2]; /* the oldest of the k - 1 previous words */
                for (int m = k - 3; m >= 0; m--)
                    head = mix_pair(head, recent[m]);
                gram = extend_gram(head, hash, k);
            }
            if (builder_push(builder, &count, gram) != TW_OK)
                return TW_ERR_NOMEM;
        }
        memmove(recent + 1, recent, (TW_NGRAM_MAX - 1) * sizeof recent[0]);
        recent[0] = hash;
        words++;
    }

    if (word_count != NULL)
        *word_count += words;
    if (count == 0)
        return TW_OK;

    uint64_t *data = builder->buf;
    qsort(data, count, sizeof *data, compare_u64);
    size_t unique = 1;
    for (size_t i = 1; i < count; i++) {
        if (data[i] != data[unique - 1])
            data[unique++] = data[i];
    }

    view->hashes = data;
    view->count = unique;
    return TW_OK;
}

tw_status tw_featureset_build(const char *line, tw_fold_mode fold, tw_featureset *out,
                              size_t *word_count)
{
    tw_featureset_builder builder;
    tw_featureset_builder_init(&builder);

    tw_featureset view;
    tw_status status = tw_featureset_builder_build(&builder, line, fold, &view, word_count);
    out->hashes = NULL;
    out->count = 0;
    if (status == TW_OK && view.count > 0) {
        out->hashes = malloc(view.count * sizeof *out->hashes);
        if (out->hashes == NULL) {
            status = TW_ERR_NOMEM;
        } else {
            memcpy(out->hashes, view.hashes, view.count * sizeof *out->hashes);
            out->count = view.count;
        }
    }

    tw_featureset_builder_free(&builder);
    return status;
}

void tw_featureset_free(tw_featureset *set)
{
    free(set->hashes);
    set->hashes = NULL;
    set->count = 0;
}

size_t tw_featureset_intersection(const tw_featureset *a, const tw_featureset *b)
{
    size_t i = 0;
    size_t j = 0;
    size_t common = 0;
    while (i < a->count && j < b->count) {
        if (a->hashes[i] < b->hashes[j]) {
            i++;
        } else if (a->hashes[i] > b->hashes[j]) {
            j++;
        } else {
            common++;
            i++;
            j++;
        }
    }
    return common;
}

double tw_featureset_jaccard(const tw_featureset *a, const tw_featureset *b)
{
    size_t common = tw_featureset_intersection(a, b);
    size_t total = a->count + b->count - common;
    return total ? (double)common / (double)total : 0.0;
}
