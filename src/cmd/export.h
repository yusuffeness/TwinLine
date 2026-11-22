/*
 * Reports of similar lines as JSON, CSV or a self-contained HTML page.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_EXPORT_H
#define TWINLINE_CMD_EXPORT_H

#include <stdbool.h>
#include <stdio.h>

#include "twinline/analysis.h"
#include "twinline/document.h"
#include "twinline/similarity.h"

typedef enum tw_export_format {
    TW_EXPORT_JSON,
    TW_EXPORT_CSV,
    TW_EXPORT_HTML,
} tw_export_format;

/* Picks the format from the file name's extension (.json, .csv, .html or
 * .htm, any case). Returns false for anything else. */
bool tw_export_format_for(const char *path, tw_export_format *format);

/* At most this many pairs are written; the report says when there were more. */
#define TW_EXPORT_MAX_PAIRS 100000

/*
 * Writes the report of `similarity` (computed on `doc` with `options`) to
 * `out`. `source` names the analysed file and may be NULL. Returns false
 * when writing failed.
 */
bool tw_export_write(FILE *out, tw_export_format format, const tw_document *doc,
                     const tw_similarity *similarity, const tw_analysis_options *options,
                     const char *source);

/*
 * The clusters as plain text for people: one block per cluster, each line
 * with its number and its similarity to the cluster's first line.
 */
bool tw_export_write_text(FILE *out, const tw_document *doc, const tw_similarity *similarity,
                          const tw_analysis_options *options, const char *source);

/*
 * An analysis result as JSON. The most similar pair is given with the full
 * text of both lines from `doc`. Fields a strategy does not compute are null.
 */
bool tw_export_result_json(FILE *out, const tw_analysis_result *result, const tw_document *doc,
                           const tw_analysis_options *options, const char *source);

#endif /* TWINLINE_CMD_EXPORT_H */
