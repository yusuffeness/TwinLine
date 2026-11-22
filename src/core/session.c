/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/session.h"

#include <stdlib.h>
#include <string.h>

void tw_buffer_init(tw_buffer *buffer)
{
    tw_document_init(&buffer->doc);
    buffer->path = NULL;
    buffer->dirty = false;
    tw_undo_init(&buffer->undo, TW_UNDO_DEFAULT_LIMIT);
    buffer->saved_state = tw_undo_state(&buffer->undo);
    buffer->page = 0;
}

void tw_buffer_free(tw_buffer *buffer)
{
    tw_document_free(&buffer->doc);
    free(buffer->path);
    buffer->path = NULL;
    tw_undo_free(&buffer->undo);
}

tw_status tw_session_init(tw_session *session)
{
    if (pthread_mutex_init(&session->lock, NULL) != 0)
        return TW_ERR_SYSTEM;
    tw_document_init(&session->doc);
    session->path = NULL;
    session->dirty = false;
    tw_undo_init(&session->undo, TW_UNDO_DEFAULT_LIMIT);
    session->saved_state = tw_undo_state(&session->undo);
    session->page = 0;
    return TW_OK;
}

void tw_session_destroy(tw_session *session)
{
    tw_document_free(&session->doc);
    free(session->path);
    session->path = NULL;
    tw_undo_free(&session->undo);
    pthread_mutex_destroy(&session->lock);
}

static void update_dirty(tw_session *session)
{
    session->dirty = tw_undo_state(&session->undo) != session->saved_state;
}

tw_status tw_session_edit_locked(tw_session *session, size_t at, size_t remove,
                                 const char *const *texts, size_t count)
{
    tw_status status = tw_undo_apply(&session->undo, &session->doc, at, remove, texts, count);
    if (status == TW_OK)
        update_dirty(session);
    return status;
}

tw_status tw_session_undo_locked(tw_session *session, const tw_edit **edit)
{
    tw_status status = tw_undo_undo(&session->undo, &session->doc, edit);
    if (status == TW_OK)
        update_dirty(session);
    return status;
}

tw_status tw_session_redo_locked(tw_session *session, const tw_edit **edit)
{
    tw_status status = tw_undo_redo(&session->undo, &session->doc, edit);
    if (status == TW_OK)
        update_dirty(session);
    return status;
}

tw_status tw_session_replace_locked(tw_session *session, tw_document *doc, const char *path)
{
    char *copy = NULL;
    if (path != NULL) {
        copy = strdup(path);
        if (copy == NULL)
            return TW_ERR_NOMEM;
    }
    free(session->path);
    session->path = copy;
    tw_document_swap(&session->doc, doc);
    tw_undo_clear(&session->undo);
    session->saved_state = tw_undo_state(&session->undo);
    session->dirty = false;
    session->page = 0;
    return TW_OK;
}

void tw_session_mark_saved_locked(tw_session *session)
{
    session->saved_state = tw_undo_state(&session->undo);
    session->dirty = false;
}

#define SWAP(type, a, b)                                                                           \
    do {                                                                                           \
        type swap_tmp_ = (a);                                                                      \
        (a) = (b);                                                                                 \
        (b) = swap_tmp_;                                                                           \
    } while (0)

void tw_session_exchange_locked(tw_session *session, tw_buffer *buffer)
{
    SWAP(tw_document, session->doc, buffer->doc);
    SWAP(char *, session->path, buffer->path);
    SWAP(bool, session->dirty, buffer->dirty);
    SWAP(tw_undo, session->undo, buffer->undo);
    SWAP(unsigned long long, session->saved_state, buffer->saved_state);
    SWAP(size_t, session->page, buffer->page);
}

tw_status tw_session_snapshot(tw_session *session, tw_document *out)
{
    pthread_mutex_lock(&session->lock);
    tw_status status = tw_document_clone(&session->doc, out);
    pthread_mutex_unlock(&session->lock);
    return status;
}

tw_status tw_session_set_path_locked(tw_session *session, const char *path)
{
    char *copy = strdup(path);
    if (copy == NULL)
        return TW_ERR_NOMEM;
    free(session->path);
    session->path = copy;
    return TW_OK;
}
