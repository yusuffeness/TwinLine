/*
 * Unit tests for src/core/document.c
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>

#include "../tw_test.h"
#include "twinline/document.h"

TW_TEST(init_is_empty)
{
    tw_document doc;
    tw_document_init(&doc);
    TW_CHECK_EQ_SIZE(doc.count, 0);
    TW_CHECK(doc.lines == NULL);
    tw_document_free(&doc); /* freeing an empty document is allowed */
}

TW_TEST(append_copies_text)
{
    tw_document doc;
    tw_document_init(&doc);
    char text[] = "original";
    TW_CHECK_EQ_INT(tw_document_append(&doc, text), TW_OK);
    text[0] = 'X';
    TW_CHECK_EQ_STR(doc.lines[0], "original");
    tw_document_free(&doc);
}

TW_TEST(push_takes_ownership)
{
    tw_document doc;
    tw_document_init(&doc);
    char *line = malloc(4);
    TW_CHECK(line != NULL);
    memcpy(line, "abc", 4);
    TW_CHECK_EQ_INT(tw_document_push(&doc, line), TW_OK);
    TW_CHECK(doc.lines[0] == line);
    tw_document_free(&doc); /* frees `line`; leak checkers verify this */
}

TW_TEST(grows_past_initial_capacity)
{
    tw_document doc;
    tw_document_init(&doc);
    char buf[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof buf, "line %d", i);
        TW_CHECK_EQ_INT(tw_document_append(&doc, buf), TW_OK);
    }
    TW_CHECK_EQ_SIZE(doc.count, 1000);
    TW_CHECK(doc.cap >= 1000);
    TW_CHECK_EQ_STR(doc.lines[0], "line 0");
    TW_CHECK_EQ_STR(doc.lines[999], "line 999");
    tw_document_free(&doc);
    TW_CHECK_EQ_SIZE(doc.count, 0);
}

TW_TEST(clone_is_deep)
{
    tw_document src;
    tw_document copy;
    tw_document_init(&src);
    TW_CHECK_EQ_INT(tw_document_append(&src, "first"), TW_OK);
    TW_CHECK_EQ_INT(tw_document_append(&src, "second"), TW_OK);

    TW_CHECK_EQ_INT(tw_document_clone(&src, &copy), TW_OK);
    TW_CHECK_EQ_SIZE(copy.count, 2);
    TW_CHECK(copy.lines[0] != src.lines[0]);
    TW_CHECK_EQ_STR(copy.lines[1], "second");

    src.lines[0][0] = 'F';
    TW_CHECK_EQ_STR(copy.lines[0], "first");

    tw_document_free(&src);
    tw_document_free(&copy);
}

TW_TEST(clone_of_empty_document)
{
    tw_document src;
    tw_document copy;
    tw_document_init(&src);
    TW_CHECK_EQ_INT(tw_document_clone(&src, &copy), TW_OK);
    TW_CHECK_EQ_SIZE(copy.count, 0);
    tw_document_free(&copy);
}

TW_TEST(swap_exchanges_contents)
{
    tw_document a;
    tw_document b;
    tw_document_init(&a);
    tw_document_init(&b);
    TW_CHECK_EQ_INT(tw_document_append(&a, "only in a"), TW_OK);

    tw_document_swap(&a, &b);
    TW_CHECK_EQ_SIZE(a.count, 0);
    TW_CHECK_EQ_SIZE(b.count, 1);
    TW_CHECK_EQ_STR(b.lines[0], "only in a");

    tw_document_free(&a);
    tw_document_free(&b);
}

int main(void)
{
    puts("document");
    TW_RUN(init_is_empty);
    TW_RUN(append_copies_text);
    TW_RUN(push_takes_ownership);
    TW_RUN(grows_past_initial_capacity);
    TW_RUN(clone_is_deep);
    TW_RUN(clone_of_empty_document);
    TW_RUN(swap_exchanges_contents);
    return tw_test_summary();
}
