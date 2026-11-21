/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/document.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TW_DOCUMENT_MIN_CAP 64

void tw_document_init(tw_document *doc)
{
    doc->lines = NULL;
    doc->count = 0;
    doc->cap = 0;
}

void tw_document_free(tw_document *doc)
{
    for (size_t i = 0; i < doc->count; i++)
        free(doc->lines[i]);
    free(doc->lines);
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
