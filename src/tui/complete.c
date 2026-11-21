/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/complete.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool tw_fuzzy_match(const char *pattern, const char *text, int *score, uint64_t *bits)
{
    *score = 0;
    *bits = 0;
    size_t t = 0;
    size_t last = (size_t)-1;
    for (size_t p = 0; pattern[p] != '\0'; p++) {
        char want = lower(pattern[p]);
        while (text[t] != '\0' && lower(text[t]) != want)
            t++;
        if (text[t] == '\0')
            return false;

        int points = 10;
        if (last != (size_t)-1 && t == last + 1)
            points += 15; /* consecutive */
        if (t == 0 || text[t - 1] == '-' || text[t - 1] == '/' || text[t - 1] == ' ')
            points += 20; /* start of a word */
        if (last != (size_t)-1)
            points -= (int)(t - last - 1 > 10 ? 10 : t - last - 1); /* gap */
        *score += points;
        if (t < 64)
            *bits |= (uint64_t)1 << t;
        last = t;
        t++;
    }
    return true;
}

static int compare_strings(const void *lhs, const void *rhs)
{
    return strcmp(*(char *const *)lhs, *(char *const *)rhs);
}

static tw_status push(tw_completions *out, size_t *cap, char *item)
{
    if (out->count == *cap) {
        size_t new_cap = *cap ? *cap * 2 : 16;
        char **items = realloc(out->items, new_cap * sizeof *items);
        if (items == NULL)
            return TW_ERR_NOMEM;
        out->items = items;
        *cap = new_cap;
    }
    out->items[out->count++] = item;
    return TW_OK;
}

tw_status tw_complete_path(const char *prefix, tw_completions *out)
{
    out->items = NULL;
    out->count = 0;

    const char *slash = strrchr(prefix, '/');
    size_t dir_len = slash ? (size_t)(slash - prefix) + 1 : 0;
    const char *base = prefix + dir_len;
    size_t base_len = strlen(base);

    char *dir = malloc(dir_len + 2);
    if (dir == NULL)
        return TW_ERR_NOMEM;
    if (dir_len == 0) {
        memcpy(dir, ".", 2);
    } else {
        memcpy(dir, prefix, dir_len);
        dir[dir_len] = '\0';
    }

    DIR *handle = opendir(dir);
    if (handle == NULL) {
        free(dir);
        return TW_OK; /* nothing to complete */
    }

    size_t cap = 0;
    tw_status status = TW_OK;
    struct dirent *entry;
    /* readdir is safe here: the stream is private to this call. */
    while (status == TW_OK && (entry = readdir(handle)) != NULL) { // NOLINT(concurrency-mt-unsafe)
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (name[0] == '.' && base[0] != '.')
            continue;
        if (strncmp(name, base, base_len) != 0)
            continue;

        size_t name_len = strlen(name);
        char *full = malloc(dir_len + name_len + 2);
        if (full == NULL) {
            status = TW_ERR_NOMEM;
            break;
        }
        memcpy(full, prefix, dir_len);
        memcpy(full + dir_len, name, name_len + 1);

        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            full[dir_len + name_len] = '/';
            full[dir_len + name_len + 1] = '\0';
        }
        status = push(out, &cap, full);
        if (status != TW_OK)
            free(full);
    }
    closedir(handle);
    free(dir);

    if (status != TW_OK) {
        tw_completions_free(out);
        return status;
    }
    if (out->count > 1)
        qsort(out->items, out->count, sizeof *out->items, compare_strings);
    return TW_OK;
}

void tw_completions_free(tw_completions *completions)
{
    for (size_t i = 0; i < completions->count; i++)
        free(completions->items[i]);
    free(completions->items);
    completions->items = NULL;
    completions->count = 0;
}

size_t tw_completions_common_prefix(const tw_completions *c)
{
    if (c->count == 0)
        return 0;
    size_t len = strlen(c->items[0]);
    for (size_t i = 1; i < c->count; i++) {
        size_t j = 0;
        while (j < len && c->items[i][j] == c->items[0][j])
            j++;
        len = j;
    }
    /* Never cut a UTF-8 character in half. */
    while (len > 0 && ((unsigned char)c->items[0][len] & 0xC0) == 0x80)
        len--;
    return len;
}
