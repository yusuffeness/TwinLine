/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/document.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define TW_DOCUMENT_MIN_CAP 64

void tw_document_init(tw_document *doc)
{
    doc->lines = NULL;
    doc->count = 0;
    doc->cap = 0;
    doc->map = NULL;
    doc->map_len = 0;
}

/* Lines inside the file mapping belong to it, not to the allocator. */
static bool is_mapped(const tw_document *doc, const char *line)
{
    if (doc->map == NULL)
        return false;
    uintptr_t p = (uintptr_t)line;
    uintptr_t base = (uintptr_t)doc->map;
    return p >= base && p < base + doc->map_len;
}

static void free_line(const tw_document *doc, char *line)
{
    if (!is_mapped(doc, line))
        free(line);
}

void tw_document_free(tw_document *doc)
{
    for (size_t i = 0; i < doc->count; i++)
        free_line(doc, doc->lines[i]);
    free(doc->lines);
    if (doc->map != NULL)
        munmap(doc->map, doc->map_len);
    tw_document_init(doc);
}

static tw_status reserve(tw_document *doc, size_t needed)
{
    if (needed <= doc->cap)
        return TW_OK;

    size_t new_cap = doc->cap ? doc->cap : TW_DOCUMENT_MIN_CAP;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2)
            return TW_ERR_NOMEM;
        new_cap *= 2;
    }
    if (new_cap > SIZE_MAX / sizeof *doc->lines)
        return TW_ERR_NOMEM;

    char **lines = realloc(doc->lines, new_cap * sizeof *lines);
    if (lines == NULL)
        return TW_ERR_NOMEM;
    doc->lines = lines;
    doc->cap = new_cap;
    return TW_OK;
}

tw_status tw_document_push(tw_document *doc, char *line)
{
    tw_status status = reserve(doc, doc->count + 1);
    if (status != TW_OK)
        return status;
    doc->lines[doc->count++] = line;
    return TW_OK;
}

tw_status tw_document_append(tw_document *doc, const char *text)
{
    char *line = strdup(text);
    if (line == NULL)
        return TW_ERR_NOMEM;

    tw_status status = tw_document_push(doc, line);
    if (status != TW_OK)
        free(line);
    return status;
}

tw_status tw_document_splice(tw_document *doc, size_t at, size_t remove, char **insert,
                             size_t count)
{
    if (at > doc->count || remove > doc->count - at)
        return TW_ERR_INVALID;
    if (count > remove) {
        if (count - remove > SIZE_MAX - doc->count)
            return TW_ERR_NOMEM;
        tw_status status = reserve(doc, doc->count + (count - remove));
        if (status != TW_OK)
            return status;
    }

    for (size_t i = 0; i < remove; i++)
        free_line(doc, doc->lines[at + i]);
    size_t tail = doc->count - at - remove;
    if (count != remove && tail > 0)
        memmove(doc->lines + at + count, doc->lines + at + remove, tail * sizeof *doc->lines);
    if (count > 0)
        memcpy(doc->lines + at, insert, count * sizeof *doc->lines);
    doc->count = doc->count - remove + count;
    return TW_OK;
}

tw_status tw_document_splice_copy(tw_document *doc, size_t at, size_t remove,
                                  const char *const *texts, size_t count)
{
    if (at > doc->count || remove > doc->count - at)
        return TW_ERR_INVALID;
    char **copies = NULL;
    if (count > 0) {
        copies = calloc(count, sizeof *copies);
        if (copies == NULL)
            return TW_ERR_NOMEM;
    }
    tw_status status = TW_OK;
    for (size_t i = 0; i < count && status == TW_OK; i++) {
        copies[i] = strdup(texts[i]);
        if (copies[i] == NULL)
            status = TW_ERR_NOMEM;
    }
    if (status == TW_OK)
        status = tw_document_splice(doc, at, remove, copies, count);
    if (status != TW_OK) {
        for (size_t i = 0; i < count; i++)
            free(copies[i]);
    }
    free(copies);
    return status;
}

tw_status tw_document_clone(const tw_document *src, tw_document *dst)
{
    tw_document_init(dst);
    if (src->count == 0)
        return TW_OK;

    tw_status status = reserve(dst, src->count);
    if (status != TW_OK)
        return status;

    for (size_t i = 0; i < src->count; i++) {
        dst->lines[i] = strdup(src->lines[i]);
        if (dst->lines[i] == NULL) {
            dst->count = i;
            tw_document_free(dst);
            return TW_ERR_NOMEM;
        }
    }
    dst->count = src->count;
    return TW_OK;
}

void tw_document_swap(tw_document *a, tw_document *b)
{
    tw_document tmp = *a;
    *a = *b;
    *b = tmp;
}
