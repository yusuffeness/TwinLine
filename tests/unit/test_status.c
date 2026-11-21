/*
 * Unit tests for src/core/status.c
 *
 * SPDX-License-Identifier: MIT
 */
#include "../tw_test.h"
#include "twinline/status.h"

TW_TEST(every_status_has_a_description)
{
    const tw_status all[] = {TW_OK,          TW_ERR_NOMEM, TW_ERR_IO,    TW_ERR_CANCELLED,
                             TW_ERR_INVALID, TW_ERR_BUSY,  TW_ERR_SYSTEM};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        const char *text = tw_status_str(all[i]);
        TW_CHECK(text != NULL);
        TW_CHECK(text[0] != '\0');
        TW_CHECK(strcmp(text, "unknown error") != 0);
    }
}

TW_TEST(descriptions_are_distinct)
{
    TW_CHECK(strcmp(tw_status_str(TW_ERR_IO), tw_status_str(TW_ERR_NOMEM)) != 0);
    TW_CHECK_EQ_STR(tw_status_str(TW_OK), "success");
    TW_CHECK_EQ_STR(tw_status_str(TW_ERR_CANCELLED), "cancelled");
}

int main(void)
{
    puts("status");
    TW_RUN(every_status_has_a_description);
    TW_RUN(descriptions_are_distinct);
    return tw_test_summary();
}
