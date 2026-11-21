/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/session.h"

#include <stdlib.h>
#include <string.h>

tw_status tw_session_init(tw_session *session)
{
    if (pthread_mutex_init(&session->lock, NULL) != 0)
        return TW_ERR_SYSTEM;
    tw_document_init(&session->doc);
    session->path = NULL;
    session->dirty = false;
    return TW_OK;
}

void tw_session_destroy(tw_session *session)
{
    tw_document_free(&session->doc);
    free(session->path);
    session->path = NULL;
    pthread_mutex_destroy(&session->lock);
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
