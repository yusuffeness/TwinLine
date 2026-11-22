/*
 * Loading and saving documents.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_FILEIO_H
#define TWINLINE_FILEIO_H

#include <stddef.h>

#include "twinline/document.h"
#include "twinline/status.h"

/* Files of at least this size are memory-mapped instead of read. */
#define TW_MMAP_THRESHOLD ((size_t)8 * 1024 * 1024)

/*
 * Reads a text file into `out`, one element per line. Line terminators
 * ("\n" or "\r\n") and a leading UTF-8 byte order mark are removed.
 * On TW_ERR_IO, `*err_no` holds the errno value; `out` is left empty on error.
 */
tw_status tw_file_load(const char *path, tw_document *out, int *err_no);

/*
 * Same as tw_file_load(), with the large-file mode threshold given: regular
 * files of at least `mmap_threshold` bytes are mapped privately into memory
 * and the lines point into the mapping, which saves an allocation and a copy
 * per line. Pass SIZE_MAX to always read, 0 to map every non-empty file.
 * If mapping fails the file is read normally.
 */
tw_status tw_file_load_ex(const char *path, tw_document *out, int *err_no, size_t mmap_threshold);

/*
 * Writes `doc` to `path` atomically: the content goes to a temporary file in
 * the same directory, is flushed to disk and then renamed over `path`. If
 * anything fails, the original file is left untouched. Existing file
 * permissions are preserved.
 */
tw_status tw_file_save(const char *path, const tw_document *doc, int *err_no);

#endif /* TWINLINE_FILEIO_H */
