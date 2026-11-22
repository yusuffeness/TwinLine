/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/undo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tw_undo_init(tw_undo *undo, size_t limit)
{
    memset(undo, 0, sizeof *undo);
    undo->limit = limit > 0 ? limit : 1;
    undo->next_id = 1;
}

static void free_lines(char **lines, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(lines[i]);
    free(lines);
}

static void free_edit(tw_edit *edit)
{
    free_lines(edit->before, edit->before_count);
    free_lines(edit->after, edit->after_count);
}

/* Drops edits[from ..), the ones that could still be redone. */
static void drop_from(tw_undo *undo, size_t from)
{
    for (size_t i = from; i < undo->count; i++)
        free_edit(&undo->edits[i]);
    undo->count = from;
}

void tw_undo_free(tw_undo *undo)
{
    drop_from(undo, 0);
    free(undo->edits);
    undo->edits = NULL;
    undo->cap = 0;
    undo->applied = 0;
}

void tw_undo_clear(tw_undo *undo)
{
    drop_from(undo, 0);
    undo->applied = 0;
    undo->base_id = undo->next_id++;
}

static char **copy_lines(const char *const *texts, size_t count, bool *ok)
{
    *ok = true;
    if (count == 0)
        return NULL;
    char **copies = calloc(count, sizeof *copies);
    if (copies == NULL) {
        *ok = false;
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        copies[i] = strdup(texts[i]);
        if (copies[i] == NULL) {
            free_lines(copies, i);
            *ok = false;
            return NULL;
        }
    }
    return copies;
}

static bool make_room(tw_undo *undo)
{
    if (undo->count == undo->limit) {
        /* Forget the oldest edit; the state before the next one becomes the base. */
        undo->base_id = undo->edits[0].id;
        free_edit(&undo->edits[0]);
        memmove(undo->edits, undo->edits + 1, (undo->count - 1) * sizeof *undo->edits);
        undo->count--;
        undo->applied--;
    }
    if (undo->count < undo->cap)
        return true;
    size_t cap = undo->cap ? undo->cap * 2 : 16;
    if (cap > undo->limit)
        cap = undo->limit;
    tw_edit *edits = realloc(undo->edits, cap * sizeof *edits);
    if (edits == NULL)
        return false;
    undo->edits = edits;
    undo->cap = cap;
    return true;
}

tw_status tw_undo_apply(tw_undo *undo, tw_document *doc, size_t at, size_t remove,
                        const char *const *texts, size_t count)
{
    if (at > doc->count || remove > doc->count - at)
        return TW_ERR_INVALID;

    tw_edit edit = {at, NULL, remove, NULL, count, 0};
    bool ok = true;
    edit.before = copy_lines((const char *const *)(doc->lines + at), remove, &ok);
    if (ok)
        edit.after = copy_lines(texts, count, &ok);
    if (!ok) {
        free_edit(&edit);
        return TW_ERR_NOMEM;
    }

    tw_status status = tw_document_splice_copy(doc, at, remove, texts, count);
    if (status != TW_OK) {
        free_edit(&edit);
        return status;
    }

    drop_from(undo, undo->applied);
    if (!make_room(undo)) {
        /* The document changed but the edit cannot be recorded: forget the
         * history rather than let it describe the wrong document. */
        free_edit(&edit);
        tw_undo_clear(undo);
        return TW_OK;
    }
    edit.id = undo->next_id++;
    undo->edits[undo->count++] = edit;
    undo->applied = undo->count;
    return TW_OK;
}

bool tw_undo_can_undo(const tw_undo *undo)
{
    return undo->applied > 0;
}

bool tw_undo_can_redo(const tw_undo *undo)
{
    return undo->applied < undo->count;
}

tw_status tw_undo_undo(tw_undo *undo, tw_document *doc, const tw_edit **edit)
{
    if (!tw_undo_can_undo(undo))
        return TW_ERR_INVALID;
    const tw_edit *e = &undo->edits[undo->applied - 1];
    tw_status status = tw_document_splice_copy(doc, e->at, e->after_count,
                                               (const char *const *)e->before, e->before_count);
    if (status != TW_OK)
        return status;
    undo->applied--;
    if (edit != NULL)
        *edit = e;
    return TW_OK;
}

tw_status tw_undo_redo(tw_undo *undo, tw_document *doc, const tw_edit **edit)
{
    if (!tw_undo_can_redo(undo))
        return TW_ERR_INVALID;
    const tw_edit *e = &undo->edits[undo->applied];
    tw_status status = tw_document_splice_copy(doc, e->at, e->before_count,
                                               (const char *const *)e->after, e->after_count);
    if (status != TW_OK)
        return status;
    undo->applied++;
    if (edit != NULL)
        *edit = e;
    return TW_OK;
}

unsigned long long tw_undo_state(const tw_undo *undo)
{
    return undo->applied > 0 ? undo->edits[undo->applied - 1].id : undo->base_id;
}

static void describe_lines(size_t first, size_t count, char *buf, size_t size)
{
    if (count == 1)
        snprintf(buf, size, "line %zu", first);
    else
        snprintf(buf, size, "lines %zu-%zu", first, first + count - 1);
}

void tw_edit_describe(const tw_edit *edit, char *buf, size_t size)
{
    char lines[64];
    size_t first = edit->at + 1;
    if (edit->before_count == 0) {
        snprintf(buf, size, "insert of %zu line%s at line %zu", edit->after_count,
                 edit->after_count == 1 ? "" : "s", first);
    } else if (edit->after_count == 0) {
        describe_lines(first, edit->before_count, lines, sizeof lines);
        snprintf(buf, size, "delete of %s", lines);
    } else {
        describe_lines(first, edit->before_count, lines, sizeof lines);
        snprintf(buf, size, "replace of %s", lines);
    }
}
