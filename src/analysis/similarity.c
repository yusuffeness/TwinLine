/*
 * SPDX-License-Identifier: MIT
 *
 * Overview
 *
 * 1. Index every line (analysis/index.c) and group the lines whose feature
 *    sets are identical. Each group becomes one distinct text ("unique set"),
 *    represented by its first line.
 * 2. Find similar pairs of unique sets with the chosen strategy, in worker
 *    threads that each keep their own pairs and counters.
 * 3. Merge, then join the unique sets of every pair with union-find; each
 *    component with at least two lines is a cluster.
 */
#include "twinline/similarity.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "analysis/index.h"
#include "twinline/featureset.h"

#define CHUNKS_PER_THREAD 64
#define MINHASH_VALUES    ((size_t)TW_MINHASH_BANDS * TW_MINHASH_ROWS)

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static bool is_cancelled(atomic_bool *cancel)
{
    return cancel != NULL && atomic_load_explicit(cancel, memory_order_relaxed);
}

/* The SplitMix64 finaliser: a fast, well-mixing 64-bit hash. */
static uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

/* ------------------------------------------------------------------------ */
/* Step 1: unique sets                                                       */
/* ------------------------------------------------------------------------ */

typedef struct unique_set {
    size_t offset; /* in the index arena */
    size_t count;
    size_t line;  /* first line with this set (0-based) */
    size_t group; /* index into group_start */
} unique_set;

typedef struct unique_index {
    tw_feature_index index;
    unique_set *sets; /* sorted by (count, line) */
    size_t count;
    size_t *group_start; /* group g: group_lines[group_start[g] .. group_start[g + 1]) */
    size_t *group_lines; /* ascending within a group */
} unique_index;

static void unique_free(unique_index *u)
{
    tw_index_free(&u->index);
    free(u->sets);
    free(u->group_start);
    free(u->group_lines);
    memset(u, 0, sizeof *u);
}

typedef struct keyed_entry {
    size_t count;
    uint64_t hash;
    size_t entry; /* index into index.entries */
} keyed_entry;

static int compare_keyed(const void *lhs, const void *rhs)
{
    const keyed_entry *a = lhs;
    const keyed_entry *b = rhs;
    if (a->count != b->count)
        return a->count < b->count ? -1 : 1;
    if (a->hash != b->hash)
        return a->hash < b->hash ? -1 : 1;
    return (a->entry > b->entry) - (a->entry < b->entry);
}

static int compare_unique(const void *lhs, const void *rhs)
{
    const unique_set *a = lhs;
    const unique_set *b = rhs;
    if (a->count != b->count)
        return a->count < b->count ? -1 : 1;
    return (a->line > b->line) - (a->line < b->line);
}

static bool same_set(const tw_feature_index *index, size_t x, size_t y)
{
    const tw_line_entry *a = &index->entries[x];
    const tw_line_entry *b = &index->entries[y];
    return a->count == b->count && memcmp(index->arena + a->offset, index->arena + b->offset,
                                          a->count * sizeof *index->arena) == 0;
}

static tw_status build_unique(const tw_document *doc, const tw_analysis_options *options,
                              atomic_bool *cancel, unique_index *u, size_t *word_count)
{
    memset(u, 0, sizeof *u);
    tw_status status =
        tw_index_build(doc, options->fold, options->ngram, tw_analysis_thread_count(options),
                       cancel, &u->index, word_count);
    if (status != TW_OK)
        return status;

    size_t n = u->index.count;
    keyed_entry *keyed = calloc(n ? n : 1, sizeof *keyed);
    u->sets = calloc(n ? n : 1, sizeof *u->sets);
    u->group_start = calloc(n + 1, sizeof *u->group_start);
    u->group_lines = calloc(n ? n : 1, sizeof *u->group_lines);
    if (keyed == NULL || u->sets == NULL || u->group_start == NULL || u->group_lines == NULL) {
        free(keyed);
        unique_free(u);
        return TW_ERR_NOMEM;
    }

    for (size_t i = 0; i < n; i++) {
        const tw_line_entry *e = &u->index.entries[i];
        uint64_t h = 0x9E3779B97F4A7C15ULL;
        for (size_t k = 0; k < e->count; k++)
            h = mix64(h ^ u->index.arena[e->offset + k]);
        keyed[i].count = e->count;
        keyed[i].hash = h;
        keyed[i].entry = i; /* entries are sorted by (count, line): entry order is line order */
    }
    qsort(keyed, n, sizeof *keyed, compare_keyed);

    /* Equal sets are now adjacent (a hash collision only splits a group,
     * which the pair search then reports as J = 1 and counts as identical). */
    size_t groups = 0;
    for (size_t i = 0; i < n; i++) {
        bool starts = i == 0 || keyed[i].count != keyed[i - 1].count ||
                      keyed[i].hash != keyed[i - 1].hash ||
                      !same_set(&u->index, keyed[u->group_start[groups - 1]].entry, keyed[i].entry);
        if (starts) {
            const tw_line_entry *e = &u->index.entries[keyed[i].entry];
            u->group_start[groups] = i;
            u->sets[groups].offset = e->offset;
            u->sets[groups].count = e->count;
            u->sets[groups].line = e->line_index;
            u->sets[groups].group = groups;
            groups++;
        }
        u->group_lines[i] = u->index.entries[keyed[i].entry].line_index;
    }
    u->group_start[groups] = n;
    u->count = groups;
    free(keyed);

    qsort(u->sets, u->count, sizeof *u->sets, compare_unique);
    return TW_OK;
}

static size_t group_size(const unique_index *u, size_t set)
{
    size_t g = u->sets[set].group;
    return u->group_start[g + 1] - u->group_start[g];
}

/* ------------------------------------------------------------------------ */
/* Step 2: pair search                                                       */
/* ------------------------------------------------------------------------ */

typedef struct set_pair {
    size_t x; /* indices into unique_index.sets */
    size_t y;
    double similarity;
} set_pair;

typedef struct shared_search {
    const unique_index *u;
    tw_strategy strategy;
    double threshold;
    atomic_bool *cancel;
    tw_analysis_progress *progress;
    atomic_size_t next; /* rows (jaccard), sets (signatures) or bands (minhash) */
    size_t chunk;
    uint32_t *keys; /* minhash: TW_MINHASH_BANDS band keys per set */
    uint64_t mult[MINHASH_VALUES];
    uint64_t add[MINHASH_VALUES];
} shared_search;

typedef struct searcher {
    pthread_t thread;
    size_t index;
    bool started;
    bool failed; /* out of memory */
    shared_search *search;
    set_pair *pairs;
    size_t pair_count;
    size_t pair_cap;
    unsigned long long candidates;
    unsigned long long near_pairs;
    double best;
    size_t best_a; /* lines, best_a < best_b */
    size_t best_b;
} searcher;

static void report_progress(searcher *self, unsigned long long done)
{
    tw_analysis_progress *progress = self->search->progress;
    if (progress == NULL || done == 0)
        return;
    atomic_fetch_add_explicit(&progress->pairs_done, done, memory_order_relaxed);
    atomic_fetch_add_explicit(&progress->thread_done[self->index % TW_PROGRESS_SLOTS], done,
                              memory_order_relaxed);
}

static bool better_pair(double similarity, size_t a, size_t b, double best, size_t best_a,
                        size_t best_b)
{
    if (similarity != best)
        return similarity > best;
    if (similarity <= 0.0)
        return false;
    return a < best_a || (a == best_a && b < best_b);
}

/* Records the outcome of scoring sets x and y. */
static void record(searcher *self, size_t x, size_t y, double similarity)
{
    const unique_index *u = self->search->u;
    size_t a = u->sets[x].line;
    size_t b = u->sets[y].line;
    if (a > b) {
        size_t t = a;
        a = b;
        b = t;
    }
    if (similarity < 1.0 && better_pair(similarity, a, b, self->best, self->best_a, self->best_b)) {
        self->best = similarity;
        self->best_a = a;
        self->best_b = b;
    }
    if (similarity < self->search->threshold)
        return;
    if (similarity < 1.0)
        self->near_pairs +=
            (unsigned long long)group_size(u, x) * (unsigned long long)group_size(u, y);

    if (self->pair_count == self->pair_cap) {
        size_t cap = self->pair_cap ? self->pair_cap * 2 : 256;
        set_pair *pairs = realloc(self->pairs, cap * sizeof *pairs);
        if (pairs == NULL) {
            self->failed = true;
            return;
        }
        self->pairs = pairs;
        self->pair_cap = cap;
    }
    set_pair *p = &self->pairs[self->pair_count++];
    p->x = x < y ? x : y;
    p->y = x < y ? y : x;
    p->similarity = similarity;
}

/* Scores sets x and y exactly when they can still matter; returns false if
 * the size bound already rules them out. */
static void score(searcher *self, size_t x, size_t y)
{
    const unique_index *u = self->search->u;
    const unique_set *a = &u->sets[x];
    const unique_set *b = &u->sets[y];
    size_t na = a->count < b->count ? a->count : b->count;
    size_t nb = a->count < b->count ? b->count : a->count;
    double target = self->best < self->search->threshold ? self->best : self->search->threshold;
    if ((double)na / (double)nb < target)
        return;
    size_t needed = tw_min_common_for(target, a->count, b->count);
    size_t common = tw_intersect(u->index.arena + a->offset, a->count, u->index.arena + b->offset,
                                 b->count, needed);
    self->candidates++;
    if (common != TW_NO_MATCH)
        record(self, x, y, tw_jaccard_of(common, a->count, b->count));
}

/* jaccard: row x against every larger set, with the size-ratio bound. */
static void jaccard_row(searcher *self, size_t x)
{
    const unique_index *u = self->search->u;
    size_t na = u->sets[x].count;
    for (size_t y = x + 1; y < u->count; y++) {
        double bound = (double)na / (double)u->sets[y].count;
        if (bound < self->search->threshold && bound < self->best)
            break; /* later sets are at least as large */
        score(self, x, y);
    }
}

/* MinHash hash functions: h_k(x) = high 32 bits of (mix(x) * mult[k] + add[k]). */
static void minhash_init(shared_search *search)
{
    for (size_t k = 0; k < MINHASH_VALUES; k++) {
        search->mult[k] = mix64(0x51ED270B27E1A2B3ULL + k) | 1u;
        search->add[k] = mix64(0xA0761D6478BD642FULL ^ ((uint64_t)k << 32));
    }
}

/* The band keys of set x: the minimum of each hash function over the set is
 * its signature value, and every TW_MINHASH_ROWS values are hashed into one
 * 32-bit band key. */
static void minhash_set(const shared_search *search, size_t x, uint32_t *keys)
{
    const unique_index *u = search->u;
    uint32_t mins[MINHASH_VALUES];
    for (size_t k = 0; k < MINHASH_VALUES; k++)
        mins[k] = UINT32_MAX;

    const uint64_t *hashes = u->index.arena + u->sets[x].offset;
    for (size_t i = 0; i < u->sets[x].count; i++) {
        uint64_t h = mix64(hashes[i]);
        for (size_t k = 0; k < MINHASH_VALUES; k++) {
            uint32_t v = (uint32_t)((h * search->mult[k] + search->add[k]) >> 32);
            if (v < mins[k])
                mins[k] = v;
        }
    }
    for (size_t band = 0; band < TW_MINHASH_BANDS; band++) {
        uint64_t key = band + 1;
        for (size_t r = 0; r < TW_MINHASH_ROWS; r++)
            key = mix64(key ^ mins[band * TW_MINHASH_ROWS + r]);
        keys[band] = (uint32_t)(key >> 32);
    }
}

typedef struct band_entry {
    uint32_t key;
    uint32_t set;
} band_entry;

/*
 * Sorts `n` entries by key with a stable two-pass radix sort (16 bits per
 * pass) through `tmp`; entries with equal keys keep their order. Much faster
 * than qsort for the millions of entries of a large file.
 */
static void radix_sort(band_entry *entries, band_entry *tmp, size_t n)
{
    size_t count[1u << 16];
    for (unsigned shift = 0; shift < 32; shift += 16) {
        memset(count, 0, sizeof count);
        for (size_t i = 0; i < n; i++)
            count[(entries[i].key >> shift) & 0xFFFFu]++;
        size_t sum = 0;
        for (size_t d = 0; d < (1u << 16); d++) {
            size_t c = count[d];
            count[d] = sum;
            sum += c;
        }
        for (size_t i = 0; i < n; i++)
            tmp[count[(entries[i].key >> shift) & 0xFFFFu]++] = entries[i];
        memcpy(entries, tmp, n * sizeof *entries);
    }
}

/* True if sets x and y already share an earlier band, where they were scored. */
static bool met_before(const uint32_t *keys, size_t x, size_t y, size_t band)
{
    const uint32_t *kx = keys + x * TW_MINHASH_BANDS;
    const uint32_t *ky = keys + y * TW_MINHASH_BANDS;
    for (size_t s = 0; s < band; s++) {
        if (kx[s] == ky[s])
            return true;
    }
    return false;
}

/* Scores every pair of sets that fall into the same bucket of one band.
 * `scratch` and `tmp` hold one band_entry per set each. */
static void minhash_band(searcher *self, size_t band, band_entry *scratch, band_entry *tmp)
{
    const shared_search *search = self->search;
    size_t n = search->u->count;
    for (size_t x = 0; x < n; x++) {
        scratch[x].key = search->keys[x * TW_MINHASH_BANDS + band];
        scratch[x].set = (uint32_t)x;
    }
    radix_sort(scratch, tmp, n);

    for (size_t start = 0; start < n;) {
        size_t end = start + 1;
        while (end < n && scratch[end].key == scratch[start].key)
            end++;
        for (size_t i = start; i < end && !self->failed; i++) {
            if ((i - start) % 256 == 0 && is_cancelled(search->cancel))
                return;
            for (size_t j = i + 1; j < end; j++) {
                size_t x = scratch[i].set;
                size_t y = scratch[j].set;
                if (!met_before(search->keys, x, y, band))
                    score(self, x, y);
            }
        }
        start = end;
    }
}

static void *search_main(void *arg)
{
    searcher *self = arg;
    shared_search *search = self->search;
    size_t n = search->u->count;
    band_entry *scratch = NULL;

    /* jaccard: chunks of rows; minhash: one band at a time (the signatures
     * are complete before the searchers start). */
    for (;;) {
        if (is_cancelled(search->cancel) || self->failed)
            break;
        size_t total = search->strategy == TW_STRATEGY_MINHASH ? TW_MINHASH_BANDS : n;
        size_t start =
            atomic_fetch_add_explicit(&search->next, search->chunk, memory_order_relaxed);
        if (start >= total)
            break;
        size_t end = total - start > search->chunk ? start + search->chunk : total;

        if (search->strategy == TW_STRATEGY_JACCARD) {
            unsigned long long done = 0;
            for (size_t x = start; x < end; x++) {
                jaccard_row(self, x);
                done += n - x - 1;
            }
            report_progress(self, done);
        } else {
            if (scratch == NULL) {
                scratch = malloc(2 * (n ? n : 1) * sizeof *scratch);
                if (scratch == NULL) {
                    self->failed = true;
                    break;
                }
            }
            for (size_t band = start; band < end; band++) {
                minhash_band(self, band, scratch, scratch + n);
                report_progress(self, n);
            }
        }
    }
    free(scratch);
    return NULL;
}

typedef struct signer {
    pthread_t thread;
    bool started;
    shared_search *search;
    size_t index;
} signer;

static void *sign_main(void *arg)
{
    signer *self = arg;
    shared_search *search = self->search;
    size_t n = search->u->count;
    for (;;) {
        if (is_cancelled(search->cancel))
            break;
        size_t start =
            atomic_fetch_add_explicit(&search->next, search->chunk, memory_order_relaxed);
        if (start >= n)
            break;
        size_t end = n - start > search->chunk ? start + search->chunk : n;
        for (size_t x = start; x < end; x++)
            minhash_set(search, x, search->keys + x * TW_MINHASH_BANDS);
        if (search->progress != NULL) {
            atomic_fetch_add_explicit(&search->progress->pairs_done, end - start,
                                      memory_order_relaxed);
            atomic_fetch_add_explicit(
                &search->progress->thread_done[self->index % TW_PROGRESS_SLOTS], end - start,
                memory_order_relaxed);
        }
    }
    return NULL;
}

/* MinHash phase A: signatures for every set, spread over `threads` threads. */
static void compute_signatures(shared_search *search, size_t threads)
{
    signer *signers = calloc(threads, sizeof *signers);
    if (signers == NULL) {
        /* Not enough memory for the thread table: sign on this thread. */
        signer self = {0};
        self.search = search;
        sign_main(&self);
        return;
    }
    for (size_t t = 0; t < threads; t++) {
        signers[t].search = search;
        signers[t].index = t;
    }
    for (size_t t = 1; t < threads; t++)
        signers[t].started = pthread_create(&signers[t].thread, NULL, sign_main, &signers[t]) == 0;
    sign_main(&signers[0]);
    for (size_t t = 1; t < threads; t++) {
        if (signers[t].started)
            pthread_join(signers[t].thread, NULL);
    }
    free(signers);
}

static void run_searchers(searcher *searchers, size_t count)
{
    for (size_t t = 1; t < count; t++)
        searchers[t].started =
            pthread_create(&searchers[t].thread, NULL, search_main, &searchers[t]) == 0;
    searchers[0].started = true;
    search_main(&searchers[0]);
    for (size_t t = 1; t < count; t++) {
        if (searchers[t].started)
            pthread_join(searchers[t].thread, NULL);
    }
}

/* ------------------------------------------------------------------------ */
/* Step 3: merge and cluster                                                 */
/* ------------------------------------------------------------------------ */

static size_t uf_find(size_t *parent, size_t x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]]; /* path halving */
        x = parent[x];
    }
    return x;
}

static void uf_union(size_t *parent, size_t a, size_t b)
{
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if (a != b) {
        if (a < b)
            parent[b] = a;
        else
            parent[a] = b;
    }
}

static int compare_pairs(const void *lhs, const void *rhs)
{
    const tw_pair *a = lhs;
    const tw_pair *b = rhs;
    if (a->similarity != b->similarity)
        return a->similarity > b->similarity ? -1 : 1;
    if (a->a != b->a)
        return a->a < b->a ? -1 : 1;
    return (a->b > b->b) - (a->b < b->b);
}

static int compare_size_t(const void *lhs, const void *rhs)
{
    size_t a = *(const size_t *)lhs;
    size_t b = *(const size_t *)rhs;
    return (a > b) - (a < b);
}

typedef struct cluster_ref {
    size_t root;
    size_t size;
    size_t first; /* smallest line */
} cluster_ref;

static int compare_clusters(const void *lhs, const void *rhs)
{
    const cluster_ref *a = lhs;
    const cluster_ref *b = rhs;
    if (a->size != b->size)
        return a->size > b->size ? -1 : 1;
    return (a->first > b->first) - (a->first < b->first);
}

/* Builds the clusters from the identical groups and the similar pairs. */
static tw_status build_clusters(const unique_index *u, const set_pair *pairs, size_t pair_count,
                                tw_similarity *out)
{
    size_t n = u->count;
    size_t *parent = malloc((n ? n : 1) * sizeof *parent);
    size_t *size = calloc(n ? n : 1, sizeof *size);
    size_t *first = malloc((n ? n : 1) * sizeof *first);
    if (parent == NULL || size == NULL || first == NULL) {
        free(parent);
        free(size);
        free(first);
        return TW_ERR_NOMEM;
    }
    for (size_t x = 0; x < n; x++)
        parent[x] = x;
    for (size_t i = 0; i < pair_count; i++) {
        if (pairs[i].x < n && pairs[i].y < n) /* always true; states the invariant */
            uf_union(parent, pairs[i].x, pairs[i].y);
    }

    for (size_t x = 0; x < n; x++)
        first[x] = SIZE_MAX;
    size_t clustered = 0;
    for (size_t x = 0; x < n; x++) {
        size_t root = uf_find(parent, x);
        size[root] += group_size(u, x);
        if (u->sets[x].line < first[root])
            first[root] = u->sets[x].line;
    }

    size_t clusters = 0;
    for (size_t x = 0; x < n; x++) {
        if (parent[x] == x && size[x] >= 2) {
            clusters++;
            clustered += size[x];
        }
    }

    cluster_ref *refs = calloc(clusters ? clusters : 1, sizeof *refs);
    out->cluster_start = calloc(clusters + 1, sizeof *out->cluster_start);
    out->cluster_lines = calloc(clustered ? clustered : 1, sizeof *out->cluster_lines);
    size_t *slot = calloc(n ? n : 1, sizeof *slot); /* root -> cluster index + 1 */
    tw_status status = TW_OK;
    if (refs == NULL || out->cluster_start == NULL || out->cluster_lines == NULL || slot == NULL) {
        status = TW_ERR_NOMEM;
        goto done;
    }

    size_t k = 0;
    for (size_t x = 0; x < n && k < clusters; x++) {
        if (parent[x] == x && size[x] >= 2) {
            refs[k].root = x;
            refs[k].size = size[x];
            refs[k].first = first[x];
            k++;
        }
    }
    qsort(refs, clusters, sizeof *refs, compare_clusters);
    size_t at = 0;
    for (k = 0; k < clusters; k++) {
        out->cluster_start[k] = at;
        slot[refs[k].root] = k + 1;
        at += refs[k].size;
    }
    out->cluster_start[clusters] = at;

    /* Fill each cluster with the lines of its groups, then sort them. */
    size_t *fill = calloc(clusters ? clusters : 1, sizeof *fill);
    if (fill == NULL) {
        status = TW_ERR_NOMEM;
        goto done;
    }
    for (size_t x = 0; x < n; x++) {
        size_t c = slot[uf_find(parent, x)];
        if (c == 0)
            continue;
        size_t g = u->sets[x].group;
        for (size_t i = u->group_start[g]; i < u->group_start[g + 1]; i++) {
            size_t at_line = out->cluster_start[c - 1] + fill[c - 1]++;
            if (at_line < clustered) /* always true: the sizes were counted above */
                out->cluster_lines[at_line] = u->group_lines[i];
        }
    }
    for (k = 0; k < clusters; k++)
        qsort(out->cluster_lines + out->cluster_start[k], refs[k].size, sizeof(size_t),
              compare_size_t);
    free(fill);
    out->cluster_count = clusters;

done:
    free(refs);
    free(slot);
    free(parent);
    free(size);
    free(first);
    return status;
}

static tw_status merge(searcher *searchers, size_t count, const unique_index *u, tw_similarity *out)
{
    size_t total = 0;
    for (size_t t = 0; t < count; t++) {
        searcher *s = &searchers[t];
        if (s->failed)
            return TW_ERR_NOMEM;
        total += s->pair_count;
        out->candidates += s->candidates;
        out->near_pairs += s->near_pairs;
        if (better_pair(s->best, s->best_a, s->best_b, out->best_similarity, out->best_a,
                        out->best_b)) {
            out->best_similarity = s->best;
            out->best_a = s->best_a;
            out->best_b = s->best_b;
        }
    }

    set_pair *all = malloc((total ? total : 1) * sizeof *all);
    out->pairs = malloc((total ? total : 1) * sizeof *out->pairs);
    if (all == NULL || out->pairs == NULL) {
        free(all);
        return TW_ERR_NOMEM;
    }
    size_t at = 0;
    for (size_t t = 0; t < count; t++) {
        if (searchers[t].pair_count > 0)
            memcpy(all + at, searchers[t].pairs, searchers[t].pair_count * sizeof *all);
        at += searchers[t].pair_count;
    }

    /* Identical texts split by a hash collision show up as J = 1: count them
     * as identical, like the other copies. */
    size_t kept = 0;
    for (size_t i = 0; i < total; i++) {
        size_t a = u->sets[all[i].x].line;
        size_t b = u->sets[all[i].y].line;
        if (all[i].similarity >= 1.0) {
            out->identical_pairs += (unsigned long long)group_size(u, all[i].x) *
                                    (unsigned long long)group_size(u, all[i].y);
            continue;
        }
        out->pairs[kept].a = a < b ? a : b;
        out->pairs[kept].b = a < b ? b : a;
        out->pairs[kept].similarity = all[i].similarity;
        kept++;
    }
    out->pair_count = kept;
    qsort(out->pairs, kept, sizeof *out->pairs, compare_pairs);

    tw_status status = build_clusters(u, all, total, out);
    free(all);
    return status;
}

/* ------------------------------------------------------------------------ */
/* Public entry points                                                       */
/* ------------------------------------------------------------------------ */

void tw_similarity_free(tw_similarity *similarity)
{
    free(similarity->pairs);
    free(similarity->cluster_start);
    free(similarity->cluster_lines);
    memset(similarity, 0, sizeof *similarity);
}

size_t tw_similarity_cluster_size(const tw_similarity *similarity, size_t k)
{
    if (k >= similarity->cluster_count)
        return 0;
    return similarity->cluster_start[k + 1] - similarity->cluster_start[k];
}

tw_status tw_similarity_run(const tw_document *doc, const tw_analysis_options *options,
                            atomic_bool *cancel, tw_analysis_progress *progress, tw_similarity *out)
{
    memset(out, 0, sizeof *out);
    if (!(options->threshold > 0.0 && options->threshold <= 1.0))
        return TW_ERR_INVALID;
    double start = now_seconds();
    out->strategy = options->strategy;
    out->threshold = options->threshold;
    out->lines_total = doc->count;

    unique_index u;
    tw_status status = build_unique(doc, options, cancel, &u, &out->words_total);
    if (status != TW_OK)
        return status;
    out->lines_with_words = u.index.count;
    out->unique_sets = u.count;
    for (size_t x = 0; x < u.count; x++) {
        unsigned long long c = group_size(&u, x);
        out->identical_pairs += c * (c - 1) / 2;
    }

    size_t threads = tw_analysis_thread_count(options);
    if (threads > u.count && u.count > 0)
        threads = u.count;
    if (threads == 0)
        threads = 1;
    out->threads = threads;

    shared_search search;
    memset(&search, 0, sizeof search);
    search.u = &u;
    search.strategy = options->strategy;
    search.threshold = options->threshold;
    search.cancel = cancel;
    search.progress = progress;

    unsigned long long work = 0;
    if (options->strategy == TW_STRATEGY_JACCARD)
        work = u.count > 1 ? (unsigned long long)u.count * (u.count - 1) / 2 : 0;
    else if (options->strategy == TW_STRATEGY_MINHASH)
        work = (unsigned long long)u.count * (1 + TW_MINHASH_BANDS);
    if (progress != NULL) {
        atomic_store(&progress->pairs_total, work);
        atomic_store(&progress->threads, threads);
    }

    searcher *searchers = calloc(threads, sizeof *searchers);
    if (searchers == NULL) {
        unique_free(&u);
        return TW_ERR_NOMEM;
    }
    for (size_t t = 0; t < threads; t++) {
        searchers[t].search = &search;
        searchers[t].index = t;
    }

    if (options->strategy == TW_STRATEGY_MINHASH && u.count > 1) {
        search.keys = malloc(u.count * TW_MINHASH_BANDS * sizeof *search.keys);
        if (search.keys == NULL) {
            free(searchers);
            unique_free(&u);
            return TW_ERR_NOMEM;
        }
        minhash_init(&search);
        search.chunk = u.count / (threads * CHUNKS_PER_THREAD);
        if (search.chunk == 0)
            search.chunk = 1;
        atomic_init(&search.next, 0);
        compute_signatures(&search, threads);
        search.chunk = 1; /* one band at a time */
        atomic_init(&search.next, 0);
        run_searchers(searchers, threads < TW_MINHASH_BANDS ? threads : TW_MINHASH_BANDS);
    } else if (options->strategy == TW_STRATEGY_JACCARD && u.count > 1) {
        search.chunk = u.count / (threads * CHUNKS_PER_THREAD);
        if (search.chunk == 0)
            search.chunk = 1;
        atomic_init(&search.next, 0);
        run_searchers(searchers, threads);
    }

    if (is_cancelled(cancel))
        status = TW_ERR_CANCELLED;
    else
        status = merge(searchers, threads, &u, out);

    for (size_t t = 0; t < threads; t++)
        free(searchers[t].pairs);
    free(searchers);
    free(search.keys);
    unique_free(&u);

    if (status != TW_OK) {
        tw_similarity_free(out);
        return status;
    }
    out->seconds = now_seconds() - start;
    return TW_OK;
}

void tw_similarity_to_result(const tw_similarity *similarity, const tw_document *doc,
                             tw_analysis_result *out)
{
    memset(out, 0, sizeof *out);
    out->strategy = similarity->strategy;
    out->threshold = similarity->threshold;
    out->threads = similarity->threads;
    out->lines_total = similarity->lines_total;
    out->lines_with_words = similarity->lines_with_words;
    out->words_total = similarity->words_total;
    out->unique_lines = similarity->unique_sets;
    size_t rows = similarity->lines_with_words;
    out->pairs = rows > 1 ? (unsigned long long)rows * (unsigned long long)(rows - 1) / 2 : 0;
    out->compared = similarity->candidates;
    out->identical = similarity->identical_pairs;
    out->near = similarity->near_pairs;
    out->best_similarity = similarity->best_similarity;
    if (similarity->best_similarity > 0.0) {
        out->best_a = similarity->best_a + 1;
        out->best_b = similarity->best_b + 1;
        tw_snippets(doc->lines[similarity->best_a], doc->lines[similarity->best_b],
                    out->best_a_text, out->best_b_text);
    }
    out->seconds = similarity->seconds;
}

double tw_line_similarity(const char *a, const char *b, tw_fold_mode fold, int ngram)
{
    tw_featureset_builder ba;
    tw_featureset_builder bb;
    tw_featureset_builder_init(&ba);
    tw_featureset_builder_init(&bb);
    tw_featureset fa;
    tw_featureset fb;
    double similarity = -1.0;
    if (tw_featureset_builder_build_ngram(&ba, a, fold, ngram, &fa, NULL) == TW_OK &&
        tw_featureset_builder_build_ngram(&bb, b, fold, ngram, &fb, NULL) == TW_OK) {
        if (fa.count == 0 && fb.count == 0)
            similarity = 1.0;
        else
            similarity = tw_featureset_jaccard(&fa, &fb);
    }
    tw_featureset_builder_free(&ba);
    tw_featureset_builder_free(&bb);
    return similarity;
}
