/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/featureset.h"

#include <stdlib.h>

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

typedef struct hash_buffer {
    uint64_t *data;
    size_t count;
    size_t cap;
} hash_buffer;

static tw_status hash_buffer_push(hash_buffer *buf, uint64_t value)
{
    if (buf->count == buf->cap) {
        size_t new_cap = buf->cap ? buf->cap * 2 : 32;
        uint64_t *data = realloc(buf->data, new_cap * sizeof *data);
        if (data == NULL)
            return TW_ERR_NOMEM;
        buf->data = data;
        buf->cap = new_cap;
    }
    buf->data[buf->count++] = value;
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

tw_status tw_featureset_build(const char *line, tw_fold_mode fold, tw_featureset *out,
                              size_t *word_count)
{
    hash_buffer buf = {NULL, 0, 0};
    const unsigned char *p = (const unsigned char *)line;
    uint64_t prev = 0;
    size_t words = 0;

    out->hashes = NULL;
    out->count = 0;

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

        if (hash_buffer_push(&buf, hash) != TW_OK ||
            (words > 0 && hash_buffer_push(&buf, mix_pair(prev, hash)) != TW_OK)) {
            free(buf.data);
            return TW_ERR_NOMEM;
        }
        prev = hash;
        words++;
    }

    if (word_count != NULL)
        *word_count += words;
    if (buf.count == 0) {
        free(buf.data);
        return TW_OK;
    }

    qsort(buf.data, buf.count, sizeof *buf.data, compare_u64);
    size_t unique = 1;
    for (size_t i = 1; i < buf.count; i++) {
        if (buf.data[i] != buf.data[unique - 1])
            buf.data[unique++] = buf.data[i];
    }

    out->hashes = buf.data;
    out->count = unique;
    return TW_OK;
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
