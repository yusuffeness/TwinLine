/*
 * Unit tests for src/core/fileio.c
 *
 * SPDX-License-Identifier: MIT
 */
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../tw_test.h"
#include "twinline/fileio.h"

static char temp_dir[256];

static const char *temp_path(const char *name)
{
    static char path[512];
    snprintf(path, sizeof path, "%s/%s", temp_dir, name);
    return path;
}

static bool write_raw(const char *name, const char *content, size_t len)
{
    FILE *file = fopen(temp_path(name), "wb");
    if (file == NULL)
        return false;
    bool ok = fwrite(content, 1, len, file) == len;
    return fclose(file) == 0 && ok;
}

/* Writes a string literal without its terminating NUL; the length is computed
 * by the compiler so it can never be miscounted. */
#define WRITE_LITERAL(name, literal) write_raw((name), (literal), sizeof(literal) - 1)

static size_t count_temp_files(void)
{
    DIR *dir = opendir(temp_dir);
    if (dir == NULL)
        return (size_t)-1;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".twinline-tmp.") != NULL)
            count++;
    }
    closedir(dir);
    return count;
}

TW_TEST(load_splits_lines)
{
    TW_CHECK(WRITE_LITERAL("plain.txt", "one\ntwo\nthree\n"));
    tw_document doc;
    int err = 0;
    TW_CHECK_EQ_INT(tw_file_load(temp_path("plain.txt"), &doc, &err), TW_OK);
    TW_CHECK_EQ_SIZE(doc.count, 3);
    TW_CHECK_EQ_STR(doc.lines[0], "one");
    TW_CHECK_EQ_STR(doc.lines[2], "three");
    tw_document_free(&doc);
}

TW_TEST(load_handles_crlf_bom_and_missing_final_newline)
{
    TW_CHECK(WRITE_LITERAL("windows.txt", "\xEF\xBB\xBF"
                                          "first\r\nsecond"));
    tw_document doc;
    int err = 0;
    TW_CHECK_EQ_INT(tw_file_load(temp_path("windows.txt"), &doc, &err), TW_OK);
    TW_CHECK_EQ_SIZE(doc.count, 2);
    TW_CHECK_EQ_STR(doc.lines[0], "first");
    TW_CHECK_EQ_STR(doc.lines[1], "second");
    tw_document_free(&doc);
}

TW_TEST(load_keeps_empty_lines)
{
    TW_CHECK(WRITE_LITERAL("blank.txt", "a\n\nb\n\n"));
    tw_document doc;
    int err = 0;
    TW_CHECK_EQ_INT(tw_file_load(temp_path("blank.txt"), &doc, &err), TW_OK);
    TW_CHECK_EQ_SIZE(doc.count, 4);
    TW_CHECK_EQ_STR(doc.lines[1], "");
    TW_CHECK_EQ_STR(doc.lines[3], "");
    tw_document_free(&doc);
}

TW_TEST(load_empty_file)
{
    TW_CHECK(WRITE_LITERAL("empty.txt", ""));
    tw_document doc;
    int err = 0;
    TW_CHECK_EQ_INT(tw_file_load(temp_path("empty.txt"), &doc, &err), TW_OK);
    TW_CHECK_EQ_SIZE(doc.count, 0);
    tw_document_free(&doc);
}

TW_TEST(load_missing_file_reports_errno)
{
    tw_document doc;
    int err = 0;
    TW_CHECK_EQ_INT(tw_file_load(temp_path("does-not-exist.txt"), &doc, &err), TW_ERR_IO);
    TW_CHECK_EQ_INT(err, ENOENT);
    TW_CHECK_EQ_SIZE(doc.count, 0);
}

TW_TEST(save_and_load_round_trip)
{
    tw_document doc;
    tw_document_init(&doc);
    TW_CHECK_EQ_INT(tw_document_append(&doc, "alpha"), TW_OK);
    TW_CHECK_EQ_INT(tw_document_append(&doc, ""), TW_OK);
    TW_CHECK_EQ_INT(tw_document_append(&doc, "\xC3\xA7"
                                             "ay"),
                    TW_OK);

    int err = 0;
    TW_CHECK_EQ_INT(tw_file_save(temp_path("round.txt"), &doc, &err), TW_OK);
    tw_document_free(&doc);

    TW_CHECK_EQ_INT(tw_file_load(temp_path("round.txt"), &doc, &err), TW_OK);
    TW_CHECK_EQ_SIZE(doc.count, 3);
    TW_CHECK_EQ_STR(doc.lines[0], "alpha");
    TW_CHECK_EQ_STR(doc.lines[1], "");
    TW_CHECK_EQ_STR(doc.lines[2], "\xC3\xA7"
                                  "ay");
    tw_document_free(&doc);
    TW_CHECK_EQ_SIZE(count_temp_files(), 0);
}

TW_TEST(save_replaces_content_and_keeps_permissions)
{
    TW_CHECK(WRITE_LITERAL("private.txt", "old content\n"));
    TW_CHECK(chmod(temp_path("private.txt"), 0600) == 0);

    tw_document doc;
    tw_document_init(&doc);
    TW_CHECK_EQ_INT(tw_document_append(&doc, "new content"), TW_OK);
    int err = 0;
    TW_CHECK_EQ_INT(tw_file_save(temp_path("private.txt"), &doc, &err), TW_OK);
    tw_document_free(&doc);

    struct stat st;
    TW_CHECK(stat(temp_path("private.txt"), &st) == 0);
    TW_CHECK_EQ_INT(st.st_mode & 0777, 0600);

    TW_CHECK_EQ_INT(tw_file_load(temp_path("private.txt"), &doc, &err), TW_OK);
    TW_CHECK_EQ_SIZE(doc.count, 1);
    TW_CHECK_EQ_STR(doc.lines[0], "new content");
    tw_document_free(&doc);
}

TW_TEST(save_into_missing_directory_fails_cleanly)
{
    tw_document doc;
    tw_document_init(&doc);
    TW_CHECK_EQ_INT(tw_document_append(&doc, "x"), TW_OK);
    int err = 0;
    TW_CHECK_EQ_INT(tw_file_save(temp_path("no/such/dir/file.txt"), &doc, &err), TW_ERR_IO);
    TW_CHECK_EQ_INT(err, ENOENT);
    tw_document_free(&doc);
    TW_CHECK_EQ_SIZE(count_temp_files(), 0);
}

/* Large-file mode: the same parsing rules, with lines inside the mapping. */
TW_TEST(mapped_load_matches_normal_load)
{
    static const char content[] = "\xEF\xBB\xBF"
                                  "first\r\nsecond\n\nfourth\r\r\nlast without newline";
    TW_CHECK(WRITE_LITERAL("mapped.txt", content));
    tw_document mapped;
    tw_document plain;
    int err = 0;
    TW_CHECK_EQ_INT(tw_file_load_ex(temp_path("mapped.txt"), &mapped, &err, 0), TW_OK);
    TW_CHECK_EQ_INT(tw_file_load_ex(temp_path("mapped.txt"), &plain, &err, SIZE_MAX), TW_OK);
    TW_CHECK(mapped.map != NULL);
    TW_CHECK(plain.map == NULL);
    TW_CHECK_EQ_SIZE(mapped.count, plain.count);
    TW_CHECK_EQ_SIZE(mapped.count, 5);
    for (size_t i = 0; i < mapped.count && i < plain.count; i++)
        TW_CHECK_EQ_STR(mapped.lines[i], plain.lines[i]);
    TW_CHECK_EQ_STR(mapped.lines[0], "first");
    TW_CHECK_EQ_STR(mapped.lines[3], "fourth");
    TW_CHECK_EQ_STR(mapped.lines[4], "last without newline");

    /* A mapped document can be edited, cloned and saved like any other. */
    static const char *const text[] = {"new line"};
    TW_CHECK_EQ_INT(tw_document_splice_copy(&mapped, 1, 2, text, 1), TW_OK);
    tw_document copy;
    TW_CHECK_EQ_INT(tw_document_clone(&mapped, &copy), TW_OK);
    TW_CHECK(copy.map == NULL);
    TW_CHECK_EQ_STR(copy.lines[1], "new line");
    TW_CHECK_EQ_INT(tw_file_save(temp_path("mapped.txt"), &mapped, &err), TW_OK);
    tw_document_free(&mapped);

    tw_document again;
    TW_CHECK_EQ_INT(tw_file_load_ex(temp_path("mapped.txt"), &again, &err, 0), TW_OK);
    TW_CHECK_EQ_SIZE(again.count, 4);
    TW_CHECK_EQ_STR(again.lines[3], "last without newline");
    tw_document_free(&again);
    tw_document_free(&copy);
    tw_document_free(&plain);
}

TW_TEST(mapped_load_small_and_empty_files)
{
    tw_document doc;
    int err = 0;
    /* Below the threshold: read normally. */
    TW_CHECK(WRITE_LITERAL("small.txt", "a\nb\n"));
    TW_CHECK_EQ_INT(tw_file_load_ex(temp_path("small.txt"), &doc, &err, 1024), TW_OK);
    TW_CHECK(doc.map == NULL);
    TW_CHECK_EQ_SIZE(doc.count, 2);
    tw_document_free(&doc);
    /* Empty files cannot be mapped. */
    TW_CHECK(WRITE_LITERAL("empty-map.txt", ""));
    TW_CHECK_EQ_INT(tw_file_load_ex(temp_path("empty-map.txt"), &doc, &err, 0), TW_OK);
    TW_CHECK(doc.map == NULL);
    TW_CHECK_EQ_SIZE(doc.count, 0);
    tw_document_free(&doc);
    /* A missing file is still reported. */
    TW_CHECK_EQ_INT(tw_file_load_ex(temp_path("nope.txt"), &doc, &err, 0), TW_ERR_IO);
    TW_CHECK_EQ_INT(err, ENOENT);
}

static void remove_temp_dir(void)
{
    DIR *dir = opendir(temp_dir);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                unlink(temp_path(entry->d_name));
        }
        closedir(dir);
    }
    rmdir(temp_dir);
}

int main(void)
{
    const char *base = getenv("TMPDIR");
    snprintf(temp_dir, sizeof temp_dir, "%s/twinline-test-XXXXXX",
             (base != NULL && *base != '\0') ? base : "/tmp");
    if (mkdtemp(temp_dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }

    puts("fileio");
    TW_RUN(load_splits_lines);
    TW_RUN(load_handles_crlf_bom_and_missing_final_newline);
    TW_RUN(load_keeps_empty_lines);
    TW_RUN(load_empty_file);
    TW_RUN(load_missing_file_reports_errno);
    TW_RUN(save_and_load_round_trip);
    TW_RUN(save_replaces_content_and_keeps_permissions);
    TW_RUN(save_into_missing_directory_fails_cleanly);
    TW_RUN(mapped_load_matches_normal_load);
    TW_RUN(mapped_load_small_and_empty_files);

    remove_temp_dir();
    return tw_test_summary();
}
