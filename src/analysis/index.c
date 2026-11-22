/*
 * SPDX-License-Identifier: MIT
 */
#include "analysis/index.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "twinline/analysis.h"
#include "twinline/featureset.h"

double tw_jaccard_of(size_t common, size_t na, size_t nb)
{
    return (double)common / (double)(na + nb - common);
}

/*
 * Smallest intersection size c for which tw_jaccard_of(c, na, nb) >= target,
 * computed with the same floating-point expression used for scoring so the
 * filter can never disagree with the score. Returns 0 when target <= 0.
 */
size_t tw_min_common_for(double target, size_t na, size_t nb)
{
    if (target <= 0.0)
        return 0;
    size_t c = (size_t)(target * (double)(na + nb) / (1.0 + target));
    if (c > na)
        c = na;
    while (c > 0 && tw_jaccard_of(c - 1, na, nb) >= target)
        c--;
    while (c <= na && tw_jaccard_of(c, na, nb) < target)
        c++;
    return c;
}

/*
 * Size of the intersection of two sorted, unique hash arrays. Stops early
 * and returns TW_NO_MATCH as soon as the remaining elements cannot lift the
 * intersection to `needed`; pass 0 to always get the exact size.
 */
size_t tw_intersect(const uint64_t *a, size_t na, const uint64_t *b, size_t nb, size_t needed)
{
    size_t i = 0;
    size_t j = 0;
    size_t common = 0;
    while (i < na && j < nb) {
        if (a[i] == b[j]) {
            common++;
            i++;
            j++;
            continue;
        }
        if (a[i] < b[j])
            i++;
        else
            j++;
        size_t left = (na - i < nb - j) ? na - i : nb - j;
        if (common + left < needed)
            return TW_NO_MATCH;
    }
    return common;
}

/* ------------------------------------------------------------------------ */
/* Step 1: feature index                                                     */
/* ------------------------------------------------------------------------ */

void tw_index_free(tw_feature_index *index)
{
    free(index->arena);
    free(index->entries);
    memset(index, 0, sizeof *index);
}

static tw_status arena_append(tw_feature_index *index, const uint64_t *hashes, size_t count)
{
    if (index->arena_len + count > index->arena_cap) {
        size_t new_cap = index->arena_cap ? index->arena_cap : 4096;
        while (new_cap < index->arena_len + count) {
            if (new_cap > SIZE_MAX / 2 / sizeof *index->arena)
                return TW_ERR_NOMEM;
            new_cap *= 2;
        }
        uint64_t *arena = realloc(index->arena, new_cap * sizeof *arena);
        if (arena == NULL)
            return TW_ERR_NOMEM;
        index->arena = arena;
        index->arena_cap = new_cap;
    }
    memcpy(index->arena + index->arena_len, hashes, count * sizeof *hashes);
    index->arena_len += count;
    return TW_OK;
}

static int compare_entries(const void *lhs, const void *rhs)
{
    const tw_line_entry *a = lhs;
    const tw_line_entry *b = rhs;
    if (a->count != b->count)
        return a->count < b->count ? -1 : 1;
    return (a->line_index > b->line_index) - (a->line_index < b->line_index);
}

/* One thread's share of the lines: [first, last). */
typedef struct index_part {
    const tw_document *doc;
    tw_fold_mode fold;
    int ngram;
    atomic_bool *cancel;
    size_t first;
    size_t last;
    tw_feature_index local;
    size_t words;
    tw_status status;
    pthread_t thread;
    bool started;
} index_part;

static void *index_part_main(void *arg)
{
    index_part *part = arg;
    tw_feature_index *index = &part->local;
    memset(index, 0, sizeof *index);
    size_t lines = part->last - part->first;
    index->entries = calloc(lines ? lines : 1, sizeof *index->entries);
    if (index->entries == NULL) {
        part->status = TW_ERR_NOMEM;
        return NULL;
    }

    tw_featureset_builder builder;
    tw_featureset_builder_init(&builder);
    tw_status status = TW_OK;
    for (size_t i = part->first; i < part->last; i++) {
        if ((i - part->first) % TW_CANCEL_CHECK_LINES == 0 && part->cancel != NULL &&
            atomic_load_explicit(part->cancel, memory_order_relaxed)) {
            status = TW_ERR_CANCELLED;
            break;
        }
        tw_featureset view;
        status = tw_featureset_builder_build_ngram(&builder, part->doc->lines[i], part->fold,
                                                   part->ngram, &view, &part->words);
        if (status != TW_OK)
            break;
        if (view.count == 0)
            continue;
        size_t offset = index->arena_len;
        status = arena_append(index, view.hashes, view.count);
        if (status != TW_OK)
            break;
        index->entries[index->count].offset = offset;
        index->entries[index->count].count = view.count;
        index->entries[index->count].line_index = i;
        index->count++;
    }
    tw_featureset_builder_free(&builder);
    part->status = status;
    return NULL;
}

/* Joins the parts, in order, into `index`. */
static tw_status join_parts(index_part *parts, size_t count, tw_feature_index *index)
{
    size_t arena_len = 0;
    size_t entries = 0;
    for (size_t p = 0; p < count; p++) {
        arena_len += parts[p].local.arena_len;
        entries += parts[p].local.count;
    }
    index->arena = malloc((arena_len ? arena_len : 1) * sizeof *index->arena);
    index->entries = malloc((entries ? entries : 1) * sizeof *index->entries);
    if (index->arena == NULL || index->entries == NULL)
        return TW_ERR_NOMEM;
    index->arena_cap = arena_len;
    for (size_t p = 0; p < count; p++) {
        const tw_feature_index *local = &parts[p].local;
        if (local->arena_len > 0)
            memcpy(index->arena + index->arena_len, local->arena,
                   local->arena_len * sizeof *index->arena);
        for (size_t e = 0; e < local->count && index->count < entries; e++) {
            index->entries[index->count] = local->entries[e];
            index->entries[index->count].offset += index->arena_len;
            index->count++;
        }
        index->arena_len += local->arena_len;
    }
    return TW_OK;
}

#define TW_INDEX_LINES_PER_THREAD 4096 /* smaller documents are indexed on one thread */

tw_status tw_index_build(const tw_document *doc, tw_fold_mode fold, int ngram, size_t threads,
                         atomic_bool *cancel, tw_feature_index *index, size_t *word_count)
{
    memset(index, 0, sizeof *index);
    size_t max_parts = doc->count / TW_INDEX_LINES_PER_THREAD;
    if (threads > max_parts)
        threads = max_parts;
    if (threads == 0)
        threads = 1;

    index_part *parts = calloc(threads, sizeof *parts);
    if (parts == NULL)
        return TW_ERR_NOMEM;
    for (size_t p = 0; p < threads; p++) {
        parts[p].doc = doc;
        parts[p].fold = fold;
        parts[p].ngram = ngram;
        parts[p].cancel = cancel;
        parts[p].first = doc->count * p / threads;
        parts[p].last = doc->count * (p + 1) / threads;
    }
    for (size_t p = 1; p < threads; p++)
        parts[p].started = pthread_create(&parts[p].thread, NULL, index_part_main, &parts[p]) == 0;
    index_part_main(&parts[0]);
    for (size_t p = 1; p < threads; p++) {
        if (parts[p].started)
            pthread_join(parts[p].thread, NULL);
        else
            index_part_main(&parts[p]); /* no thread: index the share here */
    }

    tw_status status = TW_OK;
    for (size_t p = 0; p < threads && status == TW_OK; p++)
        status = parts[p].status;
    if (status == TW_OK) {
        if (threads == 1) {
            *index = parts[0].local; /* nothing to join: take the part over */
            parts[0].local.arena = NULL;
            parts[0].local.entries = NULL;
            parts[0].local.arena_len = 0;
            parts[0].local.count = 0;
        } else {
            status = join_parts(parts, threads, index);
        }
    }
    for (size_t p = 0; p < threads; p++) {
        if (word_count != NULL)
            *word_count += parts[p].words;
        tw_index_free(&parts[p].local);
    }
    free(parts);
    if (status != TW_OK) {
        tw_index_free(index);
        return status;
    }

    qsort(index->entries, index->count, sizeof *index->entries, compare_entries);
    return TW_OK;
}

/* Copies up to TW_SNIPPET_BYTES of `src + offset`, marking cut ends with "...". */
static void copy_snippet(char *dst, const char *src, size_t offset)
{
    if (offset > 0) {
        memcpy(dst, "...", 3);
        dst += 3;
        src += offset;
    }
    size_t len = tw_utf8_prefix_len(src, TW_SNIPPET_BYTES);
    memcpy(dst, src, len);
    if (src[len] != '\0')
        memcpy(dst + len, "...", 4);
    else
        dst[len] = '\0';
}

/*
 * Where the snippets of two similar lines should start: when their first
 * difference would be cut off, start two words before it so the reader
 * sees what changed. Both lines share the bytes before the difference, so
 * the offset is a word boundary in both.
 */
static size_t snippet_offset(const char *a, const char *b)
{
    size_t diverge = 0;
    while (a[diverge] != '\0' && a[diverge] == b[diverge])
        diverge++;
    if (diverge <= TW_SNIPPET_BYTES / 2)
        return 0;
    size_t start = diverge;
    for (int words = 0; words < 3 && start > 0; words++) {
        while (start > 0 && a[start - 1] != ' ')
            start--; /* to the start of this word */
        if (words < 2)
            while (start > 0 && a[start - 1] == ' ')
                start--; /* over the gap before it */
    }
    return start;
}

void tw_snippets(const char *a, const char *b, char *out_a, char *out_b)
{
    size_t offset = snippet_offset(a, b);
    copy_snippet(out_a, a, offset);
    copy_snippet(out_b, b, offset);
}
