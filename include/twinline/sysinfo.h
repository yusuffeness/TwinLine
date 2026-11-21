/*
 * Information about the machine TwinLine runs on.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_SYSINFO_H
#define TWINLINE_SYSINFO_H

#include <stddef.h>

/* Number of online processors; at least 1. */
size_t tw_cpu_count(void);

#endif /* TWINLINE_SYSINFO_H */
