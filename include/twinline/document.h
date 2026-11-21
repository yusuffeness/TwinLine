/*
 * In-memory document: an ordered list of lines.
 *
 * A tw_document is a plain value type and is NOT thread-safe on its own.
 * Shared documents are protected by the lock in tw_session.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_DOCUMENT_H
#define TWINLINE_DOCUMENT_H

#include <stddef.h>

#include "twinline/status.h"

typedef struct tw_document {
    char **lines; /* each line is NUL-terminated and contains no '\n' */
    size_t count; /* number of lines */
    size_t cap;   /* allocated slots in `lines` */
} tw_document;

/* Initialises an empty document. */
void tw_document_init(tw_document *doc);

/* Releases every line and resets the document to empty. */
void tw_document_free(tw_document *doc);

/* Appends `line`. On success the document takes ownership of `line`;
 * on failure the caller still owns it. */
tw_status tw_document_push(tw_document *doc, char *line);

/* Appends a copy of `text`. */
tw_status tw_document_append(tw_document *doc, const char *text);

/* Makes `dst` a deep copy of `src`. `dst` must not own any memory. */
tw_status tw_document_clone(const tw_document *src, tw_document *dst);

/* Exchanges the contents of two documents in O(1). */
void tw_document_swap(tw_document *a, tw_document *b);

#endif /* TWINLINE_DOCUMENT_H */
