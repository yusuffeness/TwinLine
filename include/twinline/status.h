/*
 * Status codes shared by every TwinLine module.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_STATUS_H
#define TWINLINE_STATUS_H

typedef enum tw_status {
    TW_OK = 0,
    TW_ERR_NOMEM,     /* an allocation failed */
    TW_ERR_IO,        /* a file operation failed; see the accompanying errno */
    TW_ERR_CANCELLED, /* the operation was cancelled by the user */
    TW_ERR_INVALID,   /* invalid argument or state */
    TW_ERR_BUSY,      /* the resource is already in use */
    TW_ERR_SYSTEM,    /* an operating system call failed (threads, clocks) */
} tw_status;

/* Returns a short, human-readable description of a status code. */
const char *tw_status_str(tw_status status);

#endif /* TWINLINE_STATUS_H */
