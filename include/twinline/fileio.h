/*
 * Loading and saving documents.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_FILEIO_H
#define TWINLINE_FILEIO_H

#include "twinline/document.h"
#include "twinline/status.h"

/*
 * Reads a text file into `out`, one element per line. Line terminators
 * ("\n" or "\r\n") and a leading UTF-8 byte order mark are removed.
 * On TW_ERR_IO, `*err_no` holds the errno value; `out` is left empty on error.
 */
tw_status tw_file_load(const char *path, tw_document *out, int *err_no);

/*
 * Writes `doc` to `path` atomically: the content goes to a temporary file in
 * the same directory, is flushed to disk and then renamed over `path`. If
 * anything fails, the original file is left untouched. Existing file
 * permissions are preserved.
 */
tw_status tw_file_save(const char *path, const tw_document *doc, int *err_no);

#endif /* TWINLINE_FILEIO_H */
