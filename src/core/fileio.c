/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/fileio.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TW_UTF8_BOM     "\xEF\xBB\xBF"
#define TW_TEMP_SUFFIX  ".twinline-tmp.XXXXXX"
#define TW_DEFAULT_MODE 0644

static void strip_line_ending(char *buf, size_t *len)
{
    while (*len > 0 && (buf[*len - 1] == '\n' || buf[*len - 1] == '\r'))
        buf[--*len] = '\0';
}

tw_status tw_file_load(const char *path, tw_document *out, int *err_no)
{
    *err_no = 0;
    tw_document_init(out);

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        *err_no = errno;
        return TW_ERR_IO;
    }

    tw_document doc;
    tw_document_init(&doc);

    char *buf = NULL;
    size_t buf_cap = 0;
    bool first_line = true;
    tw_status status = TW_OK;

    errno = 0;
    for (;;) {
        ssize_t read = getline(&buf, &buf_cap, file);
        if (read < 0)
            break;

        size_t len = (size_t)read;
        strip_line_ending(buf, &len);

        const char *start = buf;
        if (first_line && len >= 3 && memcmp(buf, TW_UTF8_BOM, 3) == 0)
            start += 3;
        first_line = false;

        status = tw_document_append(&doc, start);
        if (status != TW_OK)
            break;
    }

    if (status == TW_OK && !feof(file)) {
        status = (errno == ENOMEM) ? TW_ERR_NOMEM : TW_ERR_IO;
        *err_no = errno ? errno : EIO;
    }

    free(buf);
    fclose(file);

    if (status != TW_OK) {
        tw_document_free(&doc);
        return status;
    }
    *out = doc;
    return TW_OK;
}

static int write_lines(FILE *file, const tw_document *doc)
{
    for (size_t i = 0; i < doc->count; i++) {
        if (fputs(doc->lines[i], file) == EOF || fputc('\n', file) == EOF)
            return errno ? errno : EIO;
    }
    if (fflush(file) != 0)
        return errno ? errno : EIO;
    if (fsync(fileno(file)) != 0)
        return errno;
    return 0;
}

tw_status tw_file_save(const char *path, const tw_document *doc, int *err_no)
{
    *err_no = 0;

    size_t path_len = strlen(path);
    char *temp_path = malloc(path_len + sizeof TW_TEMP_SUFFIX);
    if (temp_path == NULL)
        return TW_ERR_NOMEM;
    memcpy(temp_path, path, path_len);
    memcpy(temp_path + path_len, TW_TEMP_SUFFIX, sizeof TW_TEMP_SUFFIX);

    int fd = mkstemp(temp_path);
    if (fd < 0) {
        *err_no = errno;
        free(temp_path);
        return TW_ERR_IO;
    }

    /* mkstemp creates the file with mode 0600; keep the original mode if the
     * target exists, otherwise use the conventional 0644. Best effort only. */
    struct stat st;
    mode_t mode = (stat(path, &st) == 0) ? (st.st_mode & 07777) : TW_DEFAULT_MODE;
    (void)fchmod(fd, mode);

    FILE *file = fdopen(fd, "w");
    if (file == NULL) {
        *err_no = errno;
        close(fd);
        unlink(temp_path);
        free(temp_path);
        return TW_ERR_IO;
    }

    int err = write_lines(file, doc);
    if (fclose(file) != 0 && err == 0)
        err = errno ? errno : EIO;
    if (err == 0 && rename(temp_path, path) != 0)
        err = errno;

    if (err != 0) {
        unlink(temp_path);
        free(temp_path);
        *err_no = err;
        return TW_ERR_IO;
    }

    free(temp_path);
    return TW_OK;
}
