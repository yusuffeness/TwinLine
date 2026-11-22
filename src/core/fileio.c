/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/fileio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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

/*
 * Splits a private, writable mapping of the whole file into lines in place:
 * each line terminator becomes a NUL. A last line without a terminator has
 * no room for one inside the mapping, so it is copied.
 */
static tw_status split_mapping(char *map, size_t size, tw_document *doc)
{
    char *p = map;
    char *end = map + size;
    if (size >= 3 && memcmp(map, TW_UTF8_BOM, 3) == 0)
        p += 3;

    while (p < end) {
        char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = (size_t)((nl != NULL ? nl : end) - p);
        while (len > 0 && p[len - 1] == '\r')
            len--;

        tw_status status;
        if (nl != NULL || len < (size_t)(end - p)) {
            p[len] = '\0'; /* overwrites the '\n' or a '\r' */
            status = tw_document_push(doc, p);
        } else {
            char *copy = malloc(len + 1);
            if (copy == NULL)
                return TW_ERR_NOMEM;
            memcpy(copy, p, len);
            copy[len] = '\0';
            status = tw_document_push(doc, copy);
            if (status != TW_OK)
                free(copy);
        }
        if (status != TW_OK)
            return status;
        if (nl == NULL)
            break;
        p = nl + 1;
    }
    return TW_OK;
}

/* Returns true and fills `out` when the file was mapped; false to fall back. */
static bool load_mapped(const char *path, size_t threshold, tw_document *out, tw_status *status,
                        int *err_no)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false; /* the stdio path reports the error */
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uintmax_t)st.st_size < threshold || (uintmax_t)st.st_size > SIZE_MAX) {
        close(fd);
        return false;
    }
    size_t size = (size_t)st.st_size;
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd); /* the mapping stays valid */
    if (map == MAP_FAILED)
        return false;

    tw_document doc;
    tw_document_init(&doc);
    doc.map = map;
    doc.map_len = size;
    *status = split_mapping(map, size, &doc);
    if (*status != TW_OK) {
        tw_document_free(&doc); /* also unmaps */
        *err_no = 0;
        return true;
    }
    *out = doc;
    return true;
}

tw_status tw_file_load(const char *path, tw_document *out, int *err_no)
{
    return tw_file_load_ex(path, out, err_no, TW_MMAP_THRESHOLD);
}

tw_status tw_file_load_ex(const char *path, tw_document *out, int *err_no, size_t mmap_threshold)
{
    *err_no = 0;
    tw_document_init(out);

    tw_status mapped_status = TW_OK;
    if (mmap_threshold != SIZE_MAX &&
        load_mapped(path, mmap_threshold, out, &mapped_status, err_no))
        return mapped_status;

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
        ssize_t nread = getline(&buf, &buf_cap, file);
        if (nread < 0)
            break;

        size_t len = (size_t)nread;
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
    mode_t mode = TW_DEFAULT_MODE;
    if (stat(path, &st) == 0)
        mode = (mode_t)(st.st_mode & 07777);
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
