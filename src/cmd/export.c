/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/export.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "tui/widgets.h"
#include "twinline/version.h"

bool tw_export_format_for(const char *path, tw_export_format *format)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL || strchr(dot, '/') != NULL)
        return false;
    char ext[8];
    size_t len = strlen(dot + 1);
    if (len == 0 || len >= sizeof ext)
        return false;
    for (size_t i = 0; i <= len; i++)
        ext[i] = (char)tolower((unsigned char)dot[1 + i]);
    if (strcmp(ext, "json") == 0)
        *format = TW_EXPORT_JSON;
    else if (strcmp(ext, "csv") == 0)
        *format = TW_EXPORT_CSV;
    else if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)
        *format = TW_EXPORT_HTML;
    else
        return false;
    return true;
}

static const char *fold_name(tw_fold_mode fold)
{
    return fold == TW_FOLD_TURKISH ? "turkish" : "default";
}

/* Similarity of a cluster member to the cluster's first line. */
static double member_similarity(const tw_document *doc, const tw_analysis_options *options,
                                size_t first, size_t line)
{
    if (line == first)
        return 1.0;
    double j =
        tw_line_similarity(doc->lines[first], doc->lines[line], options->fold, options->ngram);
    return j < 0.0 ? 0.0 : j;
}

static size_t clustered_lines(const tw_similarity *s)
{
    return s->cluster_count > 0 ? s->cluster_start[s->cluster_count] : 0;
}

/* ------------------------------------------------------------------ JSON */

static void json_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        default:
            if (*p < 0x20)
                fprintf(out, "\\u%04x", *p);
            else
                fputc(*p, out);
        }
    }
    fputc('"', out);
}

static void write_json(FILE *out, const tw_document *doc, const tw_similarity *s,
                       const tw_analysis_options *options, const char *source)
{
    fputs("{\n  \"tool\": \"TwinLine\",\n  \"version\": ", out);
    json_string(out, TWINLINE_VERSION);
    fputs(",\n  \"file\": ", out);
    if (source != NULL)
        json_string(out, source);
    else
        fputs("null", out);
    fprintf(out,
            ",\n  \"settings\": {\"strategy\": \"%s\", \"threshold\": %.4g, \"ngram\": %d, "
            "\"fold\": \"%s\"},\n",
            tw_strategy_name(s->strategy), s->threshold, options->ngram, fold_name(options->fold));
    fprintf(out,
            "  \"summary\": {\"lines\": %zu, \"lines_with_words\": %zu, \"distinct_lines\": %zu, "
            "\"identical_pairs\": %llu, \"near_pairs\": %llu, \"clusters\": %zu, "
            "\"clustered_lines\": %zu},\n",
            s->lines_total, s->lines_with_words, s->unique_sets, s->identical_pairs, s->near_pairs,
            s->cluster_count, clustered_lines(s));

    fputs("  \"clusters\": [", out);
    for (size_t k = 0; k < s->cluster_count; k++) {
        size_t begin = s->cluster_start[k];
        size_t end = s->cluster_start[k + 1];
        size_t first = s->cluster_lines[begin];
        fprintf(out, "%s\n    {\"id\": %zu, \"size\": %zu, \"lines\": [", k ? "," : "", k + 1,
                end - begin);
        for (size_t i = begin; i < end; i++) {
            size_t line = s->cluster_lines[i];
            fprintf(out, "%s\n      {\"line\": %zu, \"similarity\": %.4f, \"text\": ",
                    i > begin ? "," : "", line + 1, member_similarity(doc, options, first, line));
            json_string(out, doc->lines[line]);
            fputc('}', out);
        }
        fputs("\n    ]}", out);
    }
    fputs(s->cluster_count ? "\n  ],\n" : "],\n", out);

    size_t pairs = s->pair_count < TW_EXPORT_MAX_PAIRS ? s->pair_count : TW_EXPORT_MAX_PAIRS;
    fputs("  \"pairs\": [", out);
    for (size_t i = 0; i < pairs; i++)
        fprintf(out, "%s\n    {\"a\": %zu, \"b\": %zu, \"similarity\": %.4f}", i ? "," : "",
                s->pairs[i].a + 1, s->pairs[i].b + 1, s->pairs[i].similarity);
    fputs(pairs ? "\n  ],\n" : "],\n", out);
    fprintf(out, "  \"pairs_truncated\": %s\n}\n", pairs < s->pair_count ? "true" : "false");
}

/* ------------------------------------------------------------------- CSV */

static void csv_field(FILE *out, const char *text)
{
    if (strpbrk(text, ",\"\r\n") == NULL && text[0] != ' ' && text[0] != '\0' &&
        text[strlen(text) - 1] != ' ') {
        fputs(text, out);
        return;
    }
    fputc('"', out);
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '"')
            fputc('"', out);
        fputc(*p, out);
    }
    fputc('"', out);
}

static void write_csv(FILE *out, const tw_document *doc, const tw_similarity *s,
                      const tw_analysis_options *options)
{
    fputs("cluster,size,line,similarity,text\r\n", out);
    for (size_t k = 0; k < s->cluster_count; k++) {
        size_t begin = s->cluster_start[k];
        size_t end = s->cluster_start[k + 1];
        size_t first = s->cluster_lines[begin];
        for (size_t i = begin; i < end; i++) {
            size_t line = s->cluster_lines[i];
            fprintf(out, "%zu,%zu,%zu,%.4f,", k + 1, end - begin, line + 1,
                    member_similarity(doc, options, first, line));
            csv_field(out, doc->lines[line]);
            fputs("\r\n", out);
        }
    }
}

/* ------------------------------------------------------------------ HTML */

static void html_text(FILE *out, const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        switch (text[i]) {
        case '&':
            fputs("&amp;", out);
            break;
        case '<':
            fputs("&lt;", out);
            break;
        case '>':
            fputs("&gt;", out);
            break;
        case '"':
            fputs("&quot;", out);
            break;
        default:
            fputc(text[i], out);
        }
    }
}

/* Writes `text` with the words whose bit is set in `changed` highlighted,
 * splitting words on spaces as tw_word_diff() does. */
static void html_diff(FILE *out, const char *text, uint64_t changed, const char *css_class)
{
    size_t word = 0;
    const char *p = text;
    while (*p != '\0') {
        if (*p == ' ') {
            fputc(' ', out);
            p++;
            continue;
        }
        const char *start = p;
        while (*p != '\0' && *p != ' ')
            p++;
        bool hit = word < 64 && ((changed >> word) & 1u);
        if (hit)
            fprintf(out, "<mark class=\"%s\">", css_class);
        html_text(out, start, (size_t)(p - start));
        if (hit)
            fputs("</mark>", out);
        word++;
    }
}

static const char HTML_STYLE[] =
    ":root{--bg:#1a1b26;--card:#24283b;--line:#414868;--text:#c0caf5;--muted:#7a83a8;"
    "--accent:#7aa2f7;--accent2:#bb9af7;--ok:#9ece6a;--bad:#f7768e}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--text);"
    "font:15px/1.5 ui-sans-serif,system-ui,-apple-system,'Segoe UI',sans-serif}"
    "main{max-width:1000px;margin:0 auto;padding:40px 24px 64px}"
    "h1{font-size:28px;margin:0 0 4px;background:linear-gradient(90deg,var(--accent),"
    "var(--accent2));-webkit-background-clip:text;background-clip:text;color:transparent}"
    ".sub{color:var(--muted);margin:0 0 28px}"
    ".stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;"
    "margin-bottom:32px}"
    ".stat{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px "
    "16px}"
    ".stat b{display:block;font-size:24px;font-variant-numeric:tabular-nums}"
    ".stat span{color:var(--muted);font-size:13px}"
    ".cluster{background:var(--card);border:1px solid var(--line);border-radius:12px;"
    "margin:0 0 16px;overflow:hidden}"
    ".cluster h2{font-size:15px;margin:0;padding:12px 16px;border-bottom:1px solid var(--line);"
    "display:flex;gap:10px;align-items:center}"
    ".cluster h2 .n{color:var(--muted);font-weight:400}"
    "table{width:100%;border-collapse:collapse}"
    "td{padding:6px 16px;vertical-align:top;border-top:1px solid #2f3449}"
    "tr:first-child td{border-top:0}"
    "td.no{color:var(--muted);text-align:right;width:72px;font-variant-numeric:tabular-nums}"
    "td.j{width:84px}"
    ".badge{display:inline-block;border-radius:999px;padding:1px 9px;font-size:12px;"
    "font-weight:700;color:#111118;background:var(--accent2)}"
    ".badge.same{background:var(--ok)}"
    "td.t{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px;"
    "white-space:pre-wrap;word-break:break-word}"
    "mark{background:none;color:var(--ok);font-weight:700;text-decoration:underline}"
    "mark.del{color:var(--bad)}"
    ".empty{color:var(--muted)}"
    "footer{color:var(--muted);font-size:13px;margin-top:32px}";

#define HTML_MAX_CLUSTERS 500 /* clusters shown in the page */

static void write_html(FILE *out, const tw_document *doc, const tw_similarity *s,
                       const tw_analysis_options *options, const char *source)
{
    fputs("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>",
          out);
    fputs("TwinLine report", out);
    if (source != NULL) {
        fputs(" - ", out);
        html_text(out, source, strlen(source));
    }
    fprintf(out, "</title>\n<style>%s</style>\n</head>\n<body>\n<main>\n", HTML_STYLE);
    fputs("<h1>TwinLine report</h1>\n<p class=\"sub\">", out);
    if (source != NULL)
        html_text(out, source, strlen(source));
    else
        fputs("Untitled document", out);
    fprintf(out, " &middot; strategy %s &middot; threshold %.2f &middot; n-grams up to %d</p>\n",
            tw_strategy_name(s->strategy), s->threshold, options->ngram);

    fprintf(out,
            "<section class=\"stats\">\n"
            "<div class=\"stat\"><b>%zu</b><span>lines</span></div>\n"
            "<div class=\"stat\"><b>%zu</b><span>distinct lines</span></div>\n"
            "<div class=\"stat\"><b>%llu</b><span>identical pairs</span></div>\n"
            "<div class=\"stat\"><b>%llu</b><span>near-duplicate pairs</span></div>\n"
            "<div class=\"stat\"><b>%zu</b><span>clusters</span></div>\n"
            "<div class=\"stat\"><b>%zu</b><span>lines in clusters</span></div>\n"
            "</section>\n",
            s->lines_total, s->unique_sets, s->identical_pairs, s->near_pairs, s->cluster_count,
            clustered_lines(s));

    if (s->cluster_count == 0)
        fputs("<p class=\"empty\">No similar lines were found.</p>\n", out);
    size_t shown = s->cluster_count < HTML_MAX_CLUSTERS ? s->cluster_count : HTML_MAX_CLUSTERS;
    for (size_t k = 0; k < shown; k++) {
        size_t begin = s->cluster_start[k];
        size_t end = s->cluster_start[k + 1];
        size_t first = s->cluster_lines[begin];
        fprintf(out,
                "<section class=\"cluster\">\n<h2>Cluster %zu <span class=\"n\">%zu lines</span>"
                "</h2>\n<table>\n",
                k + 1, end - begin);
        for (size_t i = begin; i < end; i++) {
            size_t line = s->cluster_lines[i];
            double j = member_similarity(doc, options, first, line);
            fprintf(out, "<tr><td class=\"no\">%zu</td><td class=\"j\">", line + 1);
            if (i == begin)
                fputs("<span class=\"badge same\">first</span>", out);
            else if (j >= 1.0)
                fputs("<span class=\"badge same\">same</span>", out);
            else
                fprintf(out, "<span class=\"badge\">J %.2f</span>", j);
            fputs("</td><td class=\"t\">", out);
            if (i == begin || j >= 1.0) {
                html_text(out, doc->lines[line], strlen(doc->lines[line]));
            } else {
                uint64_t gone = 0;
                uint64_t added = 0;
                tw_word_diff(doc->lines[first], doc->lines[line], &gone, &added);
                html_diff(out, doc->lines[line], added, "add");
            }
            fputs("</td></tr>\n", out);
        }
        fputs("</table>\n</section>\n", out);
    }
    if (shown < s->cluster_count)
        fprintf(out, "<p class=\"empty\">%zu more clusters are in the JSON and CSV exports.</p>\n",
                s->cluster_count - shown);
    fprintf(out,
            "<footer>Made with TwinLine %s. Highlighted words differ from the first line of "
            "their cluster.</footer>\n</main>\n</body>\n</html>\n",
            TWINLINE_VERSION);
}

/* ------------------------------------------------------------------ text */

bool tw_export_write_text(FILE *out, const tw_document *doc, const tw_similarity *s,
                          const tw_analysis_options *options, const char *source)
{
    if (s->cluster_count == 0) {
        fprintf(out, "No similar lines in %s (strategy %s, threshold %.2f).\n",
                source != NULL ? source : "the input", tw_strategy_name(s->strategy), s->threshold);
        return !ferror(out);
    }
    fprintf(out, "%zu cluster%s with %zu lines in %s (strategy %s, threshold %.2f)\n",
            s->cluster_count, s->cluster_count == 1 ? "" : "s", clustered_lines(s),
            source != NULL ? source : "the input", tw_strategy_name(s->strategy), s->threshold);
    for (size_t k = 0; k < s->cluster_count; k++) {
        size_t begin = s->cluster_start[k];
        size_t end = s->cluster_start[k + 1];
        size_t first = s->cluster_lines[begin];
        fprintf(out, "\nCluster %zu, %zu lines\n", k + 1, end - begin);
        for (size_t i = begin; i < end; i++) {
            size_t line = s->cluster_lines[i];
            double j = member_similarity(doc, options, first, line);
            char tag[16];
            if (i == begin)
                snprintf(tag, sizeof tag, "first ");
            else if (j >= 1.0)
                snprintf(tag, sizeof tag, "same  ");
            else
                snprintf(tag, sizeof tag, "J %.2f", j);
            fprintf(out, "%8zu  %s  %s\n", line + 1, tag, doc->lines[line]);
        }
    }
    return !ferror(out);
}

/* ------------------------------------------------------- analysis as JSON */

bool tw_export_result_json(FILE *out, const tw_analysis_result *r, const tw_document *doc,
                           const tw_analysis_options *options, const char *source)
{
    fputs("{\n  \"tool\": \"TwinLine\",\n  \"version\": ", out);
    json_string(out, TWINLINE_VERSION);
    fputs(",\n  \"file\": ", out);
    if (source != NULL)
        json_string(out, source);
    else
        fputs("null", out);
    fprintf(out,
            ",\n  \"settings\": {\"strategy\": \"%s\", \"threshold\": %.4g, \"ngram\": %d, "
            "\"fold\": \"%s\", \"exhaustive\": %s},\n",
            tw_strategy_name(r->strategy), r->threshold, options->ngram, fold_name(options->fold),
            r->exhaustive ? "true" : "false");
    fprintf(out,
            "  \"result\": {\n    \"lines\": %zu,\n    \"lines_with_words\": %zu,\n"
            "    \"words\": %zu,\n    \"line_pairs\": %llu,\n    \"pairs_scored\": %llu,\n",
            r->lines_total, r->lines_with_words, r->words_total, r->pairs, r->compared);
    if (r->strategy == TW_STRATEGY_JACCARD)
        fputs("    \"distinct_lines\": null,\n", out);
    else
        fprintf(out, "    \"distinct_lines\": %zu,\n", r->unique_lines);
    fprintf(out, "    \"identical_pairs\": %llu,\n", r->identical);
    if (r->strategy == TW_STRATEGY_EXACT)
        fputs("    \"near_pairs\": null,\n", out);
    else
        fprintf(out, "    \"near_pairs\": %llu,\n", r->near);
    if (r->exhaustive && r->strategy == TW_STRATEGY_JACCARD)
        fprintf(out, "    \"mean_similarity\": %.6f,\n", r->mean_similarity);
    else
        fputs("    \"mean_similarity\": null,\n", out);
    if (r->best_similarity > 0.0 && r->best_a >= 1 && r->best_b <= doc->count) {
        fprintf(out,
                "    \"most_similar\": {\"a\": %zu, \"b\": %zu, \"similarity\": %.6f, \"a_text\": ",
                r->best_a, r->best_b, r->best_similarity);
        json_string(out, doc->lines[r->best_a - 1]);
        fputs(", \"b_text\": ", out);
        json_string(out, doc->lines[r->best_b - 1]);
        fputs("},\n", out);
    } else {
        fputs("    \"most_similar\": null,\n", out);
    }
    fprintf(out, "    \"threads\": %zu,\n    \"seconds\": %.6f\n  }\n}\n", r->threads, r->seconds);
    return !ferror(out);
}

bool tw_export_write(FILE *out, tw_export_format format, const tw_document *doc,
                     const tw_similarity *similarity, const tw_analysis_options *options,
                     const char *source)
{
    switch (format) {
    case TW_EXPORT_JSON:
        write_json(out, doc, similarity, options, source);
        break;
    case TW_EXPORT_CSV:
        write_csv(out, doc, similarity, options);
        break;
    case TW_EXPORT_HTML:
        write_html(out, doc, similarity, options, source);
        break;
    }
    return !ferror(out);
}
