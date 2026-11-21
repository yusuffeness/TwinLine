/*
 * SPDX-License-Identifier: MIT
 */
#include "twinline/sysinfo.h"

#include <unistd.h>

size_t tw_cpu_count(void)
{
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (size_t)count : 1;
}
