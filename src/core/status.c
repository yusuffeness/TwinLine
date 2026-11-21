/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/status.h"

const char *tw_status_str(tw_status status)
{
    switch (status) {
    case TW_OK:
        return "success";
    case TW_ERR_NOMEM:
        return "out of memory";
    case TW_ERR_IO:
        return "input/output error";
    case TW_ERR_CANCELLED:
        return "cancelled";
    case TW_ERR_INVALID:
        return "invalid argument";
    case TW_ERR_BUSY:
        return "resource busy";
    case TW_ERR_SYSTEM:
        return "system error";
    }
    return "unknown error";
}
