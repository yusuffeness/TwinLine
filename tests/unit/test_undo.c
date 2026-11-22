/*
 * Unit tests for src/core/undo.c and the undo-aware parts of src/core/session.c
 *
 * SPDX-License-Identifier: MIT
 */
#include "../tw_test.h"
#include "twinline/session.h"
#include "twinline/undo.h"

static const char *const ONE[] = {"one"};
static const char *const TWO[] = {"two"};
static const char *const XY[] = {"x", "y"};

TW_TEST(undo_and_redo_restore_each_state)
{
    tw_document doc;
    tw_document_init(&doc);
    tw_undo undo;
    tw_undo_init(&undo, 10);
    unsigned long long empty = tw_undo_state(&undo);
    TW_CHECK(!tw_undo_can_undo(&undo));
    TW_CHECK(!tw_undo_can_redo(&undo));

    TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 0, 0, ONE, 1), TW_OK);  /* one */
    TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 1, 0, XY, 2), TW_OK);   /* one x y */
    TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 0, 1, TWO, 1), TW_OK);  /* two x y */
    TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 1, 2, NULL, 0), TW_OK); /* two */
    unsigned long long last = tw_undo_state(&undo);
    TW_CHECK_EQ_SIZE(doc.count, 1);
    TW_CHECK_EQ_STR(doc.lines[0], "two");

    const tw_edit *edit = NULL;
    char what[96];
    TW_CHECK_EQ_INT(tw_undo_undo(&undo, &doc, &edit), TW_OK);
    tw_edit_describe(edit, what, sizeof what);
    TW_CHECK_EQ_STR(what, "delete of lines 2-3");
    TW_CHECK_EQ_SIZE(doc.count, 3);
    TW_CHECK_EQ_STR(doc.lines[2], "y");

    TW_CHECK_EQ_INT(tw_undo_undo(&undo, &doc, &edit), TW_OK);
    tw_edit_describe(edit, what, sizeof what);
    TW_CHECK_EQ_STR(what, "replace of line 1");
    TW_CHECK_EQ_STR(doc.lines[0], "one");

    TW_CHECK_EQ_INT(tw_undo_undo(&undo, &doc, &edit), TW_OK);
    tw_edit_describe(edit, what, sizeof what);
    TW_CHECK_EQ_STR(what, "insert of 2 lines at line 2");
    TW_CHECK_EQ_INT(tw_undo_undo(&undo, &doc, NULL), TW_OK);
    TW_CHECK_EQ_SIZE(doc.count, 0);
    TW_CHECK(tw_undo_state(&undo) == empty);
    TW_CHECK_EQ_INT(tw_undo_undo(&undo, &doc, NULL), TW_ERR_INVALID);

    for (int i = 0; i < 4; i++)
        TW_CHECK_EQ_INT(tw_undo_redo(&undo, &doc, NULL), TW_OK);
    TW_CHECK_EQ_INT(tw_undo_redo(&undo, &doc, NULL), TW_ERR_INVALID);
    TW_CHECK(tw_undo_state(&undo) == last);
    TW_CHECK_EQ_SIZE(doc.count, 1);
    TW_CHECK_EQ_STR(doc.lines[0], "two");

    tw_undo_free(&undo);
    tw_document_free(&doc);
}

TW_TEST(new_edit_drops_redo)
{
    tw_document doc;
    tw_document_init(&doc);
    tw_undo undo;
    tw_undo_init(&undo, 10);
    TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 0, 0, ONE, 1), TW_OK);
    TW_CHECK_EQ_INT(tw_undo_undo(&undo, &doc, NULL), TW_OK);
    TW_CHECK(tw_undo_can_redo(&undo));
    TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 0, 0, TWO, 1), TW_OK);
    TW_CHECK(!tw_undo_can_redo(&undo));
    TW_CHECK_EQ_STR(doc.lines[0], "two");
    /* A bad range changes nothing. */
    TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 5, 0, ONE, 1), TW_ERR_INVALID);
    TW_CHECK_EQ_SIZE(undo.count, 1);
    tw_undo_free(&undo);
    tw_document_free(&doc);
}

TW_TEST(limit_forgets_the_oldest_edits)
{
    tw_document doc;
    tw_document_init(&doc);
    tw_undo undo;
    tw_undo_init(&undo, 3);
    for (int i = 0; i < 5; i++)
        TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 0, 0, ONE, 1), TW_OK);
    TW_CHECK_EQ_SIZE(undo.count, 3);
    int undone = 0;
    while (tw_undo_undo(&undo, &doc, NULL) == TW_OK)
        undone++;
    TW_CHECK_EQ_INT(undone, 3);
    TW_CHECK_EQ_SIZE(doc.count, 2); /* the two oldest inserts stay */
    tw_undo_free(&undo);
    tw_document_free(&doc);
}

TW_TEST(clear_starts_a_new_state)
{
    tw_document doc;
    tw_document_init(&doc);
    tw_undo undo;
    tw_undo_init(&undo, 10);
    unsigned long long before = tw_undo_state(&undo);
    TW_CHECK_EQ_INT(tw_undo_apply(&undo, &doc, 0, 0, ONE, 1), TW_OK);
    tw_undo_clear(&undo);
    TW_CHECK(!tw_undo_can_undo(&undo));
    TW_CHECK(tw_undo_state(&undo) != before);
    tw_undo_free(&undo);
    tw_document_free(&doc);
}

/* Undoing back to the saved state makes the session clean again. */
TW_TEST(session_dirty_follows_the_saved_state)
{
    tw_session s;
    TW_CHECK_EQ_INT(tw_session_init(&s), TW_OK);
    TW_CHECK(!s.dirty);
    TW_CHECK_EQ_INT(tw_session_edit_locked(&s, 0, 0, ONE, 1), TW_OK);
    TW_CHECK(s.dirty);
    tw_session_mark_saved_locked(&s);
    TW_CHECK(!s.dirty);
    TW_CHECK_EQ_INT(tw_session_edit_locked(&s, 1, 0, TWO, 1), TW_OK);
    TW_CHECK(s.dirty);
    TW_CHECK_EQ_INT(tw_session_undo_locked(&s, NULL), TW_OK);
    TW_CHECK(!s.dirty); /* back at the save */
    TW_CHECK_EQ_INT(tw_session_undo_locked(&s, NULL), TW_OK);
    TW_CHECK(s.dirty); /* before the save */
    TW_CHECK_EQ_INT(tw_session_redo_locked(&s, NULL), TW_OK);
    TW_CHECK(!s.dirty);

    /* Opening a document starts over: clean, nothing to undo. */
    tw_document doc;
    tw_document_init(&doc);
    TW_CHECK_EQ_INT(tw_document_append(&doc, "loaded"), TW_OK);
    TW_CHECK_EQ_INT(tw_session_replace_locked(&s, &doc, "file.txt"), TW_OK);
    tw_document_free(&doc);
    TW_CHECK(!s.dirty);
    TW_CHECK_EQ_STR(s.path, "file.txt");
    TW_CHECK_EQ_INT(tw_session_undo_locked(&s, NULL), TW_ERR_INVALID);

    /* Exchanging with a buffer swaps everything. */
    tw_buffer b;
    tw_buffer_init(&b);
    tw_session_exchange_locked(&s, &b);
    TW_CHECK_EQ_SIZE(s.doc.count, 0);
    TW_CHECK(s.path == NULL);
    TW_CHECK_EQ_STR(b.path, "file.txt");
    TW_CHECK_EQ_STR(b.doc.lines[0], "loaded");
    tw_buffer_free(&b);
    tw_session_destroy(&s);
}

int main(void)
{
    puts("undo");
    TW_RUN(undo_and_redo_restore_each_state);
    TW_RUN(new_edit_drops_redo);
    TW_RUN(limit_forgets_the_oldest_edits);
    TW_RUN(clear_starts_a_new_state);
    TW_RUN(session_dirty_follows_the_saved_state);
    return tw_test_summary();
}
