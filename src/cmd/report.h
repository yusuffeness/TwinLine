/*
 * Human-readable output of analysis results.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_REPORT_H
#define TWINLINE_CMD_REPORT_H

#include <stdio.h>

#include "twinline/analysis.h"

void tw_report_print(FILE *out, const char *title, const tw_analysis_result *result);

#endif /* TWINLINE_CMD_REPORT_H */
