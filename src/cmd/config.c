/*
 * SPDX-License-Identifier: MIT
 */
#include "cmd/config.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "cmd/settings.h"
#include "tui/theme.h"

void tw_config_default(tw_config *config)
{
    memset(config, 0, sizeof *config);
    tw_analysis_options_default(&config->analysis);
    snprintf(config->theme, sizeof config->theme, "%s", "dark");
    config->animations = true;
    config->color = true;
    config->autosave = true;
    config->autosave_interval = TW_AUTOSAVE_DEFAULT_INTERVAL;
    config->watch = true;
}

/* ------------------------------------------------------------- values */

typedef enum value_kind {
    VALUE_STRING,
    VALUE_INTEGER,
    VALUE_FLOAT,
    VALUE_BOOL,
} value_kind;

typedef struct value {
    value_kind kind;
    char text[256]; /* the string, or the number as written */
    long long integer;
    double number;
    bool boolean;
} value;

static const char *kind_name(value_kind kind)
{
    switch (kind) {
    case VALUE_STRING:
        return "a string";
    case VALUE_INTEGER:
        return "an integer";
    case VALUE_FLOAT:
        return "a number";
    case VALUE_BOOL:
        return "true or false";
    }
    return "a value";
}

/* Parses a value at `p`; `*end` is set past it. Returns an error message or NULL. */
static const char *parse_value(const char *p, value *out, const char **end)
{
    memset(out, 0, sizeof *out);
    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        size_t n = 0;
        while (*p != quote) {
            if (*p == '\0')
                return "unterminated string";
            char c = *p++;
            if (quote == '"' && c == '\\') {
                switch (*p++) {
                case '"':
                    c = '"';
                    break;
                case '\\':
                    c = '\\';
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 't':
                    c = '\t';
                    break;
                default:
                    return "unsupported escape in string (use \\\", \\\\, \\n or \\t)";
                }
            }
            if (n + 1 >= sizeof out->text)
                return "string is too long";
            out->text[n++] = c;
        }
        out->text[n] = '\0';
        out->kind = VALUE_STRING;
        *end = p + 1;
        return NULL;
    }
    if (strncmp(p, "true", 4) == 0 && !isalnum((unsigned char)p[4])) {
        out->kind = VALUE_BOOL;
        out->boolean = true;
        *end = p + 4;
        return NULL;
    }
    if (strncmp(p, "false", 5) == 0 && !isalnum((unsigned char)p[5])) {
        out->kind = VALUE_BOOL;
        out->boolean = false;
        *end = p + 5;
        return NULL;
    }

    /* A number: digits, sign, '.', exponent and '_' separators. */
    size_t n = 0;
    bool is_float = false;
    while (*p != '\0' && (isdigit((unsigned char)*p) || strchr("+-._eE", *p) != NULL)) {
        if (*p != '_') {
            if (n + 1 >= sizeof out->text)
                return "number is too long";
            if (*p == '.' || *p == 'e' || *p == 'E')
                is_float = true;
            out->text[n++] = *p;
        }
        p++;
    }
    out->text[n] = '\0';
    if (n == 0)
        return "expected a value (a \"string\", a number, true or false)";
    char *stop = NULL;
    errno = 0;
    if (is_float) {
        out->number = strtod(out->text, &stop);
        out->kind = VALUE_FLOAT;
    } else {
        out->integer = strtoll(out->text, &stop, 10);
        out->number = (double)out->integer;
        out->kind = VALUE_INTEGER;
    }
    if (*stop != '\0' || errno == ERANGE)
        return "not a valid number";
    *end = p;
    return NULL;
}

/* ----------------------------------------------------------- the keys */

typedef struct parser {
    const char *name;
    int line;
    char *message;
    size_t size;
    char section[32];
    char seen[32][72]; /* "section.key" pairs already set */
    size_t seen_count;
} parser;

#if defined(__GNUC__) || defined(__clang__)
#define TW_PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define TW_PRINTF_LIKE(fmt, args)
#endif

static bool fail(parser *ps, const char *fmt, ...) TW_PRINTF_LIKE(2, 3);

/* Records "name:line: problem" and returns false. */
static bool fail(parser *ps, const char *fmt, ...)
{
    char problem[320];
    va_list args;
    va_start(args, fmt);
    vsnprintf(problem, sizeof problem, fmt, args);
    va_end(args);
    snprintf(ps->message, ps->size, "%s:%d: %s", ps->name, ps->line, problem);
    return false;
}

static bool expect(parser *ps, const char *key, const value *v, value_kind kind)
{
    bool ok = v->kind == kind || (kind == VALUE_FLOAT && v->kind == VALUE_INTEGER);
    if (!ok)
        return fail(ps, "'%s' must be %s", key, kind_name(kind));
    return true;
}

static bool set_analysis(parser *ps, tw_config *config, const char *key, const value *v)
{
    char text[256];
    if (strcmp(key, "threshold") == 0) {
        if (!expect(ps, key, v, VALUE_FLOAT))
            return false;
        snprintf(text, sizeof text, "%.17g", v->number);
    } else if (strcmp(key, "threads") == 0) {
        if (v->kind == VALUE_STRING && strcmp(v->text, "auto") == 0)
            snprintf(text, sizeof text, "auto");
        else if (v->kind == VALUE_INTEGER)
            snprintf(text, sizeof text, "%lld", v->integer);
        else
            return fail(ps, "'%s' must be \"auto\" or a number of threads", key);
    } else if (strcmp(key, "exhaustive") == 0) {
        if (!expect(ps, key, v, VALUE_BOOL))
            return false;
        snprintf(text, sizeof text, "%s", v->boolean ? "on" : "off");
    } else if (strcmp(key, "ngram") == 0) {
        if (!expect(ps, key, v, VALUE_INTEGER))
            return false;
        snprintf(text, sizeof text, "%lld", v->integer);
    } else if (strcmp(key, "strategy") == 0 || strcmp(key, "fold") == 0) {
        if (!expect(ps, key, v, VALUE_STRING))
            return false;
        snprintf(text, sizeof text, "%s", v->text);
    } else {
        return fail(ps, "unknown key '%s' in [analysis]", key);
    }

    char problem[200];
    if (!tw_setting_set(&config->analysis, key, text, problem, sizeof problem))
        return fail(ps, "%s", problem);
    return true;
}

static bool set_key(parser *ps, tw_config *config, const char *key, const value *v)
{
    const char *s = ps->section;
    if (strcmp(s, "analysis") == 0)
        return set_analysis(ps, config, key, v);

    if (strcmp(s, "ui") == 0) {
        if (strcmp(key, "theme") == 0) {
            if (!expect(ps, key, v, VALUE_STRING))
                return false;
            if (strlen(v->text) >= sizeof config->theme || tw_theme_find(v->text) == NULL)
                return fail(ps, "unknown theme '%s' (try dark, light, dracula or nord)", v->text);
            memcpy(config->theme, v->text, strlen(v->text) + 1);
            return true;
        }
        if (strcmp(key, "animations") == 0) {
            if (!expect(ps, key, v, VALUE_BOOL))
                return false;
            config->animations = v->boolean;
            return true;
        }
        if (strcmp(key, "color") == 0) {
            if (!expect(ps, key, v, VALUE_BOOL))
                return false;
            config->color = v->boolean;
            return true;
        }
        return fail(ps, "unknown key '%s' in [ui]", key);
    }

    if (strcmp(s, "autosave") == 0) {
        if (strcmp(key, "enabled") == 0) {
            if (!expect(ps, key, v, VALUE_BOOL))
                return false;
            config->autosave = v->boolean;
            return true;
        }
        if (strcmp(key, "interval") == 0) {
            if (!expect(ps, key, v, VALUE_INTEGER))
                return false;
            if (v->integer < 1 || v->integer > TW_AUTOSAVE_MAX_INTERVAL)
                return fail(ps, "'%s' must be 1 to 3600 seconds", key);
            config->autosave_interval = (unsigned)v->integer;
            return true;
        }
        return fail(ps, "unknown key '%s' in [autosave]", key);
    }

    if (strcmp(s, "watch") == 0) {
        if (strcmp(key, "enabled") == 0) {
            if (!expect(ps, key, v, VALUE_BOOL))
                return false;
            config->watch = v->boolean;
            return true;
        }
        return fail(ps, "unknown key '%s' in [watch]", key);
    }

    if (s[0] == '\0')
        return fail(ps, "'%s' must be inside a table such as [analysis]", key);
    return fail(ps, "unknown table [%s]", s);
}

/* ---------------------------------------------------------- the lines */

static const char *skip_blanks(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* True if only blanks and an optional comment are left. */
static bool rest_is_empty(const char *p)
{
    p = skip_blanks(p);
    return *p == '\0' || *p == '#';
}

static bool is_key_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-';
}

static bool parse_line(parser *ps, tw_config *config, const char *line)
{
    const char *p = skip_blanks(line);
    if (*p == '\0' || *p == '#')
        return true;

    if (*p == '[') {
        p = skip_blanks(p + 1);
        size_t n = 0;
        while (is_key_char(*p) && n + 1 < sizeof ps->section)
            ps->section[n++] = *p++;
        ps->section[n] = '\0';
        p = skip_blanks(p);
        if (n == 0 || *p != ']' || !rest_is_empty(p + 1))
            return fail(ps, "%s", "expected a table name such as [analysis]");
        if (strcmp(ps->section, "analysis") != 0 && strcmp(ps->section, "ui") != 0 &&
            strcmp(ps->section, "autosave") != 0 && strcmp(ps->section, "watch") != 0)
            return fail(ps, "unknown table [%s] (tables: analysis, ui, autosave, watch)",
                        ps->section);
        return true;
    }

    char key[32];
    size_t n = 0;
    while (is_key_char(*p) && n + 1 < sizeof key)
        key[n++] = *p++;
    key[n] = '\0';
    if (n == 0)
        return fail(ps, "%s", "expected 'key = value'");
    p = skip_blanks(p);
    if (*p != '=')
        return fail(ps, "expected '=' after '%s'", key);
    p = skip_blanks(p + 1);

    value v;
    const char *end = NULL;
    const char *problem = parse_value(p, &v, &end);
    if (problem != NULL)
        return fail(ps, "%s", problem);
    if (!rest_is_empty(end))
        return fail(ps, "unexpected text after the value of '%s'", key);

    char qualified[72];
    snprintf(qualified, sizeof qualified, "%s.%s", ps->section, key);
    for (size_t i = 0; i < ps->seen_count; i++) {
        if (strcmp(ps->seen[i], qualified) == 0)
            return fail(ps, "'%s' is set twice", key);
    }
    if (ps->seen_count < sizeof ps->seen / sizeof ps->seen[0])
        snprintf(ps->seen[ps->seen_count++], sizeof ps->seen[0], "%s", qualified);
    return set_key(ps, config, key, &v);
}

bool tw_config_parse(const char *text, const char *name, tw_config *config, char *message,
                     size_t size)
{
    parser ps;
    memset(&ps, 0, sizeof ps);
    ps.name = name;
    ps.message = message;
    ps.size = size;

    const char *p = text;
    char line[1024];
    while (*p != '\0') {
        ps.line++;
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof line)
            return fail(&ps, "%s", "line is too long");
        memcpy(line, p, len);
        line[len] = '\0';
        if (len > 0 && line[len - 1] == '\r')
            line[len - 1] = '\0';
        if (!parse_line(&ps, config, line))
            return false;
        p = nl != NULL ? nl + 1 : p + len;
    }
    return true;
}

#define TW_CONFIG_MAX_BYTES ((size_t)64 * 1024)

bool tw_config_load(const char *path, tw_config *config, bool *found, char *message, size_t size)
{
    *found = false;
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT)
            return true;
        snprintf(message, size, "%s: %s", path, strerror(errno)); // NOLINT(concurrency-mt-unsafe)
        return false;
    }
    *found = true;
    char *text = malloc(TW_CONFIG_MAX_BYTES + 1);
    if (text == NULL) {
        fclose(file);
        snprintf(message, size, "%s: out of memory", path);
        return false;
    }
    size_t len = fread(text, 1, TW_CONFIG_MAX_BYTES + 1, file);
    bool read_error = ferror(file) != 0;
    fclose(file);
    if (read_error || len > TW_CONFIG_MAX_BYTES) {
        snprintf(message, size, "%s: %s", path,
                 read_error ? "cannot be read" : "is larger than 64 KiB");
        free(text);
        return false;
    }
    text[len] = '\0';
    bool ok = tw_config_parse(text, path, config, message, size);
    free(text);
    return ok;
}

char *tw_config_default_path(void)
{
    // NOLINTBEGIN(concurrency-mt-unsafe): read once at startup
    const char *explicit_path = getenv("TWINLINE_CONFIG");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    // NOLINTEND(concurrency-mt-unsafe)
    char buf[4096];
    if (explicit_path != NULL && *explicit_path != '\0')
        snprintf(buf, sizeof buf, "%s", explicit_path);
    else if (xdg != NULL && *xdg != '\0')
        snprintf(buf, sizeof buf, "%s/twinline/config.toml", xdg);
    else if (home != NULL && *home != '\0')
        snprintf(buf, sizeof buf, "%s/.config/twinline/config.toml", home);
    else
        return NULL;
    return strdup(buf);
}

void tw_config_print(FILE *out, const tw_config *c)
{
    const tw_analysis_options *a = &c->analysis;
    fputs("[analysis]\n", out);
    fprintf(out, "threshold = %.2f\n", a->threshold);
    if (a->threads == 0)
        fputs("threads = \"auto\"\n", out);
    else
        fprintf(out, "threads = %zu\n", a->threads);
    fprintf(out, "strategy = \"%s\"\n", tw_strategy_name(a->strategy));
    fprintf(out, "exhaustive = %s\n", a->exhaustive ? "true" : "false");
    fprintf(out, "fold = \"%s\"\n", a->fold == TW_FOLD_TURKISH ? "turkish" : "default");
    fprintf(out, "ngram = %d\n", a->ngram);
    fputs("\n[ui]\n", out);
    fprintf(out, "theme = \"%s\"\n", c->theme);
    fprintf(out, "animations = %s\n", c->animations ? "true" : "false");
    fprintf(out, "color = %s\n", c->color ? "true" : "false");
    fputs("\n[autosave]\n", out);
    fprintf(out, "enabled = %s\n", c->autosave ? "true" : "false");
    fprintf(out, "interval = %u\n", c->autosave_interval);
    fputs("\n[watch]\n", out);
    fprintf(out, "enabled = %s\n", c->watch ? "true" : "false");
}
