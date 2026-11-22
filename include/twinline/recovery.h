/*
 * Recovery files: copies of unsaved work that survive a crash.
 *
 * While a buffer has unsaved changes, the autosave service writes it to a
 * recovery file next to the original: "dir/.notes.txt.twinline.swp" for
 * "dir/notes.txt". Saving the buffer or closing TwinLine normally removes the
 * file; if TwinLine stops abruptly, the file stays and is offered the next
 * time the original is opened.
 *
 * Format: the line TW_RECOVERY_MAGIC, the original path, the time it was
 * written (seconds since the epoch), then the document's lines.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_RECOVERY_H
#define TWINLINE_RECOVERY_H

#include <stdbool.h>
#include <time.h>

#include "twinline/document.h"
#include "twinline/status.h"

#define TW_RECOVERY_MAGIC  "TWINLINE-RECOVERY 1"
#define TW_RECOVERY_SUFFIX ".twinline.swp"

/* The recovery file path for `path` (a newly allocated string), or NULL
 * when out of memory. */
char *tw_recovery_path(const char *path);

/* Writes `doc` as the recovery file of `original`, atomically and readable
 * only by its owner. */
tw_status tw_recovery_write(const char *original, const tw_document *doc, int *err_no);

/*
 * Reads the recovery file of `original` into `doc`; `*saved` (may be NULL)
 * receives the time it was written. Returns TW_ERR_IO (with `*err_no`) when
 * it cannot be read and TW_ERR_INVALID when it is not a recovery file or
 * belongs to another path.
 */
tw_status tw_recovery_read(const char *original, tw_document *doc, time_t *saved, int *err_no);

/* True if `original` has a recovery file that is newer than the file
 * itself (or the file is gone). */
bool tw_recovery_pending(const char *original);

/* Deletes the recovery file of `original`, if any. */
void tw_recovery_remove(const char *original);

#endif /* TWINLINE_RECOVERY_H */
