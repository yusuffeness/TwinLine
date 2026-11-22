/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/recovery.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "twinline/fileio.h"

char *tw_recovery_path(const char *path)
{
    const char *slash = strrchr(path, '/');
    size_t dir_len = slash != NULL ? (size_t)(slash - path) + 1 : 0;
    const char *name = path + dir_len;
    size_t size = strlen(path) + 1 + sizeof TW_RECOVERY_SUFFIX;
    char *out = malloc(size);
    if (out != NULL)
        snprintf(out, size, "%.*s.%s%s", (int)dir_len, path, name, TW_RECOVERY_SUFFIX);
    return out;
}

tw_status tw_recovery_write(const char *original, const tw_document *doc, int *err_no)
{
    *err_no = 0;
    char *swap = tw_recovery_path(original);
    if (swap == NULL)
        return TW_ERR_NOMEM;

    size_t size = strlen(original) + sizeof TW_RECOVERY_MAGIC + 32;
    char *header = malloc(size);
    if (header == NULL) {
        free(swap);
        return TW_ERR_NOMEM;
    }
    snprintf(header, size, "%s\n%s\n%lld\n", TW_RECOVERY_MAGIC, original, (long long)time(NULL));
    tw_status status = tw_file_save_ex(swap, header, doc, 0600, err_no);
    free(header);
    free(swap);
    return status;
}

tw_status tw_recovery_read(const char *original, tw_document *doc, time_t *saved, int *err_no)
{
    *err_no = 0;
    tw_document_init(doc);
    char *swap = tw_recovery_path(original);
    if (swap == NULL)
        return TW_ERR_NOMEM;
    tw_document all;
    tw_status status = tw_file_load_ex(swap, &all, err_no, SIZE_MAX);
    free(swap);
    if (status != TW_OK)
        return status;

    char *end = NULL;
    if (all.count < 3 || strcmp(all.lines[0], TW_RECOVERY_MAGIC) != 0 ||
        strcmp(all.lines[1], original) != 0) {
        tw_document_free(&all);
        return TW_ERR_INVALID;
    }
    long long when = strtoll(all.lines[2], &end, 10);
    if (end == all.lines[2] || *end != '\0') {
        tw_document_free(&all);
        return TW_ERR_INVALID;
    }
    if (saved != NULL)
        *saved = (time_t)when;

    /* The document is everything after the three header lines. */
    status = tw_document_splice(&all, 0, 3, NULL, 0);
    if (status != TW_OK) {
        tw_document_free(&all);
        return status;
    }
    *doc = all;
    return TW_OK;
}

static bool newer(const struct stat *a, const struct stat *b)
{
#ifdef __APPLE__
    if (a->st_mtimespec.tv_sec != b->st_mtimespec.tv_sec)
        return a->st_mtimespec.tv_sec > b->st_mtimespec.tv_sec;
    return a->st_mtimespec.tv_nsec > b->st_mtimespec.tv_nsec;
#else
    if (a->st_mtim.tv_sec != b->st_mtim.tv_sec)
        return a->st_mtim.tv_sec > b->st_mtim.tv_sec;
    return a->st_mtim.tv_nsec > b->st_mtim.tv_nsec;
#endif
}

bool tw_recovery_pending(const char *original)
{
    char *swap = tw_recovery_path(original);
    if (swap == NULL)
        return false;
    struct stat swap_st;
    struct stat file_st;
    bool pending = false;
    if (stat(swap, &swap_st) == 0 && S_ISREG(swap_st.st_mode))
        pending = stat(original, &file_st) != 0 || !newer(&file_st, &swap_st);
    free(swap);
    return pending;
}

void tw_recovery_remove(const char *original)
{
    char *swap = tw_recovery_path(original);
    if (swap == NULL)
        return;
    if (unlink(swap) != 0 && errno != ENOENT) {
        /* Nothing useful to do: a stale file is offered again later. */
    }
    free(swap);
}
