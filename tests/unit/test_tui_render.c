/*
 * Unit tests for the drawing side of the UI: colours, themes, the canvas
 * (virtual screen) and the widgets. Screens are checked as plain text.
 *
 * SPDX-License-Identifier: MIT
 */
#include "../tw_test.h"

#include "tui/widgets.h"
#include "twinline/utf8.h"
#include <string.h>

/* Row `y` of a canvas as plain text (static buffer). */
static const char *row(const tw_canvas *canvas, int y)
{
    static char text[1024];
    tw_strbuf sb;
    tw_strbuf_init(&sb);
    tw_canvas_row_text(canvas, y, &sb);
    snprintf(text, sizeof text, "%s", tw_strbuf_str(&sb));
    tw_strbuf_free(&sb);
    return text;
}

static bool canvas_contains(const tw_canvas *canvas, const char *needle)
{
    for (int y = 0; y < canvas->height; y++) {
        if (strstr(row(canvas, y), needle) != NULL)
            return true;
    }
    return false;
}

TW_TEST(color_detection)
{
    TW_CHECK_EQ_INT(tw_color_mode_from("1", "truecolor", "xterm"), TW_COLOR_NONE);
    TW_CHECK_EQ_INT(tw_color_mode_from("", "truecolor", "xterm"), TW_COLOR_TRUE);
    TW_CHECK_EQ_INT(tw_color_mode_from(NULL, "24bit", NULL), TW_COLOR_TRUE);
    TW_CHECK_EQ_INT(tw_color_mode_from(NULL, NULL, "xterm-256color"), TW_COLOR_256);
    TW_CHECK_EQ_INT(tw_color_mode_from(NULL, NULL, "dumb"), TW_COLOR_NONE);
}

TW_TEST(palette_conversion)
{
    TW_CHECK_EQ_INT(tw_rgb_to_256(tw_rgb_hex(0x000000)), 16);
    TW_CHECK_EQ_INT(tw_rgb_to_256(tw_rgb_hex(0xFFFFFF)), 231);
    TW_CHECK_EQ_INT(tw_rgb_to_256(tw_rgb_hex(0xFF0000)), 196);
    TW_CHECK_EQ_INT(tw_rgb_to_256(tw_rgb_hex(0x808080)), 244); /* grey ramp */
    tw_rgb mid = tw_rgb_mix(tw_rgb_hex(0x000000), tw_rgb_hex(0xFF0000), 0.5);
    TW_CHECK_EQ_INT(mid.r, 128);
    TW_CHECK_EQ_INT(mid.g, 0);
}

TW_TEST(themes)
{
    TW_CHECK_EQ_SIZE(tw_theme_count(), 4);
    TW_CHECK(tw_theme_find("dracula") != NULL);
    TW_CHECK(tw_theme_find("nope") == NULL);
    TW_CHECK_EQ_STR(tw_theme_default()->name, "dark");
    TW_CHECK(tw_theme_at(4) == NULL);
}

TW_TEST(canvas_text_and_clipping)
{
    tw_canvas c;
    TW_CHECK(tw_canvas_init(&c, 10, 2));
    TW_CHECK_EQ_INT(tw_canvas_put(&c, 0, 0, "hello", tw_style_plain()), 5);
    TW_CHECK_EQ_STR(row(&c, 0), "hello");
    tw_canvas_put(&c, 7, 0, "world", tw_style_plain()); /* clipped at the edge */
    TW_CHECK_EQ_STR(row(&c, 0), "hello  wor");
    tw_canvas_put(&c, 0, 5, "off canvas", tw_style_plain());
    TW_CHECK_EQ_STR(row(&c, 1), "");
    tw_canvas_free(&c);
}

TW_TEST(canvas_wide_characters)
{
    tw_canvas c;
    TW_CHECK(tw_canvas_init(&c, 5, 1));
    /* Two-column emoji, then a letter: 2 + 1 columns. */
    TW_CHECK_EQ_INT(tw_canvas_put(&c, 0, 0,
                                  "\xF0\x9F\x98\x80"
                                  "a",
                                  tw_style_plain()),
                    3);
    /* A wide character that would cross the edge is dropped. */
    TW_CHECK_EQ_INT(tw_canvas_put(&c, 4, 0, "\xE4\xB8\xAD", tw_style_plain()), 4);
    TW_CHECK_EQ_STR(row(&c, 0), "\xF0\x9F\x98\x80"
                                "a");
    tw_canvas_free(&c);
}

TW_TEST(ansi_rendering_by_color_mode)
{
    tw_canvas c;
    TW_CHECK(tw_canvas_init(&c, 8, 1));
    tw_canvas_put(&c, 0, 0, "hi", tw_style_fg(tw_rgb_hex(0xFF0000), TW_STYLE_BOLD));

    tw_strbuf sb;
    tw_strbuf_init(&sb);
    tw_canvas_render_row(&c, 0, TW_COLOR_TRUE, &sb);
    TW_CHECK(strstr(tw_strbuf_str(&sb), "38;2;255;0;0") != NULL);
    TW_CHECK(strstr(tw_strbuf_str(&sb), ";1") != NULL);

    tw_strbuf_clear(&sb);
    tw_canvas_render_row(&c, 0, TW_COLOR_256, &sb);
    TW_CHECK(strstr(tw_strbuf_str(&sb), "38;5;196") != NULL);

    tw_strbuf_clear(&sb);
    tw_canvas_render_row(&c, 0, TW_COLOR_NONE, &sb);
    TW_CHECK(strstr(tw_strbuf_str(&sb), "38;") == NULL); /* no colour at all */
    TW_CHECK(strstr(tw_strbuf_str(&sb), "hi") != NULL);
    tw_strbuf_free(&sb);
    tw_canvas_free(&c);
}

TW_TEST(text_widths)
{
    TW_CHECK_EQ_SIZE(tw_utf8_str_width("abc"), 3);
    TW_CHECK_EQ_SIZE(tw_utf8_str_width("\xC3\xA7"
                                       "ay"),
                     3);
    TW_CHECK_EQ_SIZE(tw_utf8_str_width("\xE4\xB8\xAD\xE6\x96\x87"), 4); /* two CJK */
    TW_CHECK_EQ_SIZE(tw_utf8_str_width("e\xCC\x81"), 1);                /* combining acute */
}

TW_TEST(box_and_ellipsis)
{
    tw_canvas c;
    TW_CHECK(tw_canvas_init(&c, 12, 3));
    tw_draw_box(&c, 0, 0, 12, 3, tw_style_plain());
    tw_draw_text(&c, 2, 1, 8, "a very long text", tw_style_plain());
    TW_CHECK_EQ_STR(row(&c, 0), "╭──────────╮");
    TW_CHECK_EQ_STR(row(&c, 1), "│ a very … │");
    TW_CHECK_EQ_STR(row(&c, 2), "╰──────────╯");
    tw_canvas_free(&c);
}

TW_TEST(count_formatting)
{
    char buf[32];
    tw_format_count(0, buf, sizeof buf);
    TW_CHECK_EQ_STR(buf, "0");
    tw_format_count(999, buf, sizeof buf);
    TW_CHECK_EQ_STR(buf, "999");
    tw_format_count(16357, buf, sizeof buf);
    TW_CHECK_EQ_STR(buf, "16,357");
    tw_format_count(1234567, buf, sizeof buf);
    TW_CHECK_EQ_STR(buf, "1,234,567");
}

TW_TEST(welcome_banner)
{
    tw_welcome w = {tw_theme_default(), 100, "9.9.9", "~/notes", -1.0};
    tw_canvas c;
    TW_CHECK(tw_welcome_render(&w, &c));
    TW_CHECK(canvas_contains(&c, "████████╗"));
    TW_CHECK(canvas_contains(&c, "Welcome to TwinLine v9.9.9"));
    TW_CHECK(canvas_contains(&c, "cwd: ~/notes"));
    tw_canvas_free(&c);

    /* Narrow terminals get the banner without the big logo. */
    w.width = 40;
    TW_CHECK(tw_welcome_render(&w, &c));
    TW_CHECK(!canvas_contains(&c, "████████╗"));
    TW_CHECK(c.width <= 40);
    tw_canvas_free(&c);
}

static tw_view base_view(void)
{
    tw_view v;
    memset(&v, 0, sizeof v);
    v.theme = tw_theme_default();
    v.width = 60;
    v.input = "";
    v.lines = 16357;
    v.threads = 8;
    return v;
}

TW_TEST(view_input_box_and_status)
{
    tw_view v = base_view();
    v.input = "/open notes.txt";
    v.cursor = 5;
    v.file = "notes.txt";
    v.modified = true;

    tw_canvas c;
    int cx = 0;
    int cy = 0;
    TW_CHECK(tw_view_render(&v, &c, &cx, &cy));
    TW_CHECK_EQ_STR(row(&c, 0), "");
    TW_CHECK(strncmp(row(&c, 1), "╭", 3) == 0);
    TW_CHECK(strstr(row(&c, 2), "> /open notes.txt") != NULL);
    TW_CHECK_EQ_INT(cx, 4 + 5);
    TW_CHECK_EQ_INT(cy, 2);
    TW_CHECK(strstr(row(&c, c.height - 1), "notes.txt ● modified · 16,357 lines · 8 threads") !=
             NULL);
    tw_canvas_free(&c);
}

TW_TEST(view_placeholder_and_wrapping)
{
    tw_view v = base_view();
    tw_canvas c;
    int cx = 0;
    int cy = 0;
    TW_CHECK(tw_view_render(&v, &c, &cx, &cy));
    TW_CHECK(canvas_contains(&c, "Type a command, or / for the menu"));
    tw_canvas_free(&c);

    /* 54 columns of text per row at width 60: 60 characters need two rows. */
    char long_input[61];
    memset(long_input, 'x', 60);
    long_input[60] = '\0';
    v.input = long_input;
    v.cursor = 60;
    TW_CHECK(tw_view_render(&v, &c, &cx, &cy));
    TW_CHECK_EQ_INT(cy, 3);  /* second input row */
    TW_CHECK_EQ_INT(cx, 10); /* 4 + 6 overflowing characters */
    tw_canvas_free(&c);
}

TW_TEST(view_menu_activity_and_hint)
{
    tw_menu_item items[10];
    char labels[10][8];
    for (int i = 0; i < 10; i++) {
        snprintf(labels[i], sizeof labels[i], "/cmd%d", i);
        items[i].label = labels[i];
        items[i].detail = "does something";
        items[i].match_bits = 0;
    }
    tw_view v = base_view();
    v.input = "/c";
    v.cursor = 2;
    v.items = items;
    v.item_count = 10;
    v.selected = 9;
    v.activity = "Analyzing… 41%";
    v.hint = "a hint";

    tw_canvas c;
    int cx = 0;
    int cy = 0;
    TW_CHECK(tw_view_render(&v, &c, &cx, &cy));
    TW_CHECK(strstr(row(&c, 1), "✶ Analyzing… 41%") != NULL);
    /* The window scrolls so that the selected (last) item is visible. */
    TW_CHECK(canvas_contains(&c, "/cmd9"));
    TW_CHECK(!canvas_contains(&c, "/cmd0"));
    TW_CHECK(canvas_contains(&c, "a hint"));
    tw_canvas_free(&c);

    v.selected = 0;
    TW_CHECK(tw_view_render(&v, &c, &cx, &cy));
    TW_CHECK(canvas_contains(&c, "/cmd0"));
    TW_CHECK(canvas_contains(&c, "↓ 2 more"));
    tw_canvas_free(&c);
}

TW_TEST(compact_numbers_and_easing)
{
    char buf[16];
    tw_format_compact(999, buf, sizeof buf);
    TW_CHECK_EQ_STR(buf, "999");
    tw_format_compact(4800000, buf, sizeof buf);
    TW_CHECK_EQ_STR(buf, "4.8M");
    tw_format_compact(10864791, buf, sizeof buf);
    TW_CHECK_EQ_STR(buf, "11M");
    TW_CHECK(tw_ease_out(-1.0) == 0.0);
    TW_CHECK(tw_ease_out(2.0) == 1.0);
    TW_CHECK(tw_ease_out(0.5) > 0.5); /* fast start, slow end */
}

TW_TEST(spinner_cycles)
{
    const char *first = tw_spinner_frame(0.0);
    TW_CHECK(first[0] != '\0');
    TW_CHECK(strcmp(first, tw_spinner_frame(0.1)) != 0);
}

TW_TEST(progress_bar_has_sub_cell_steps)
{
    tw_canvas c;
    TW_CHECK(tw_canvas_init(&c, 10, 1));
    tw_rgb a = tw_rgb_hex(0x7aa2f7);
    tw_rgb b = tw_rgb_hex(0xbb9af7);
    tw_draw_progress(&c, 0, 0, 10, 0.55, a, b, a);
    TW_CHECK_EQ_STR(row(&c, 0), "█████▌░░░░");
    tw_draw_progress(&c, 0, 0, 10, 2.0, a, b, a); /* clamped */
    TW_CHECK_EQ_STR(row(&c, 0), "██████████");
    tw_draw_progress(&c, 0, 0, 10, -1.0, a, b, a);
    TW_CHECK_EQ_STR(row(&c, 0), "░░░░░░░░░░");
    tw_canvas_free(&c);
}

TW_TEST(word_diff_marks_changed_words)
{
    uint64_t a = 0;
    uint64_t b = 0;
    tw_word_diff("the quick brown fox", "the quick red fox", &a, &b);
    TW_CHECK(a == (1u << 2));
    TW_CHECK(b == (1u << 2));
    /* Case and punctuation are not differences. */
    tw_word_diff("The server answered!", "the server answered", &a, &b);
    TW_CHECK(a == 0 && b == 0);
    tw_word_diff("one two", "one two three", &a, &b);
    TW_CHECK(a == 0);
    TW_CHECK(b == (1u << 2));
}

TW_TEST(canvas_blit_and_row_compare)
{
    tw_canvas big;
    tw_canvas small;
    TW_CHECK(tw_canvas_init(&big, 8, 2));
    TW_CHECK(tw_canvas_init(&small, 3, 1));
    tw_canvas_put(&small, 0, 0, "abc", tw_style_plain());
    tw_canvas_blit(&big, &small, 6, 1); /* clipped at the right edge */
    TW_CHECK_EQ_STR(row(&big, 1), "      ab");

    tw_canvas other;
    TW_CHECK(tw_canvas_init(&other, 8, 2));
    TW_CHECK(tw_canvas_row_equal(&big, &other, 0));
    TW_CHECK(!tw_canvas_row_equal(&big, &other, 1));
    tw_canvas_put(&other, 6, 1, "ab", tw_style_plain());
    TW_CHECK(tw_canvas_row_equal(&big, &other, 1));
    tw_canvas_put(&other, 6, 1, "ab", tw_style_fg(tw_rgb_hex(0xff0000), 0));
    TW_CHECK(!tw_canvas_row_equal(&big, &other, 1)); /* same text, other colour */
    tw_canvas_free(&big);
    tw_canvas_free(&small);
    tw_canvas_free(&other);
}

TW_TEST(view_progress_threads_and_toast)
{
    double fractions[2] = {0.46, 0.43};
    tw_activity act = {"Analyzing", 0.4, 0.44, 4800000, 10864791, 2, true, false, fractions, 2};
    tw_view v = base_view();
    v.width = 90;
    v.progress = &act;
    v.busy = true;
    v.animate = true;
    v.time = 1.0;
    v.toast = "Saved 3 lines";
    v.toast_kind = TW_TOAST_SUCCESS;
    v.toast_age = 1.0;

    tw_canvas c;
    int cx = 0;
    int cy = 0;
    TW_CHECK(tw_view_render(&v, &c, &cx, &cy));
    TW_CHECK(strstr(row(&c, 0), "Saved 3 lines") != NULL);
    TW_CHECK(canvas_contains(&c, "44% · 4.8M / 11M pairs · 2 threads · esc to cancel"));
    TW_CHECK(canvas_contains(&c, "T1"));
    TW_CHECK(canvas_contains(&c, "46%"));
    TW_CHECK(canvas_contains(&c, "T2"));
    tw_canvas_free(&c);

    act.cancelling = true;
    v.toast = NULL;
    v.animate = false;
    TW_CHECK(tw_view_render(&v, &c, &cx, &cy));
    TW_CHECK(!canvas_contains(&c, "Saved 3 lines"));
    TW_CHECK(!canvas_contains(&c, "esc to cancel"));
    tw_canvas_free(&c);
}

TW_TEST(result_card_counts_up)
{
    tw_analysis_result r;
    memset(&r, 0, sizeof r);
    r.threshold = 0.7;
    r.threads = 2;
    r.lines_total = 4662;
    r.pairs = 10864791;
    r.compared = 120000;
    r.identical = 36;
    r.near = 40;
    r.best_similarity = 0.86;
    r.best_a = 917;
    r.best_b = 1327;
    r.seconds = 1.04;
    snprintf(r.best_a_text, sizeof r.best_a_text, "the quick brown fox");
    snprintf(r.best_b_text, sizeof r.best_b_text, "the quick red fox");

    tw_result_card card = {tw_theme_default(), 100, &r, 1.0};
    tw_canvas c;
    TW_CHECK(tw_card_render(&card, &c));
    TW_CHECK(canvas_contains(&c, "✔ Analysis finished in 1.04 s with 2 threads"));
    TW_CHECK(canvas_contains(&c, "4,662 lines · 10,864,791 line pairs · 120,000 compared"));
    TW_CHECK(canvas_contains(&c, "36 identical pairs"));
    TW_CHECK(canvas_contains(&c, "40 near duplicates (J ≥ 0.70)"));
    TW_CHECK(canvas_contains(&c, "Most similar pair  J 0.86"));
    TW_CHECK(canvas_contains(&c, "917  the quick brown fox"));
    TW_CHECK(canvas_contains(&c, "1327  the quick red fox"));
    tw_canvas_free(&c);

    /* At the start of the count-up the numbers are still zero. */
    card.progress = 0.0;
    TW_CHECK(tw_card_render(&card, &c));
    TW_CHECK(!canvas_contains(&c, "✔"));
    TW_CHECK(canvas_contains(&c, "0 identical pairs"));
    tw_canvas_free(&c);

    /* No similar pair: no pair section. */
    r.best_similarity = 0.0;
    r.exhaustive = true;
    r.mean_similarity = 0.0242;
    card.progress = 1.0;
    TW_CHECK(tw_card_render(&card, &c));
    TW_CHECK(canvas_contains(&c, "mean J 0.0242"));
    TW_CHECK(!canvas_contains(&c, "Most similar"));
    tw_canvas_free(&c);
}

TW_TEST(welcome_shine_keeps_text)
{
    tw_welcome w = {tw_theme_default(), 100, "9.9.9", "~/notes", 20.0};
    tw_canvas c;
    TW_CHECK(tw_welcome_render(&w, &c));
    TW_CHECK(canvas_contains(&c, "████████╗"));
    TW_CHECK(canvas_contains(&c, "Welcome to TwinLine v9.9.9"));
    tw_canvas_free(&c);
}

int main(void)
{
    puts("tui rendering");
    TW_RUN(color_detection);
    TW_RUN(palette_conversion);
    TW_RUN(themes);
    TW_RUN(canvas_text_and_clipping);
    TW_RUN(canvas_wide_characters);
    TW_RUN(ansi_rendering_by_color_mode);
    TW_RUN(text_widths);
    TW_RUN(box_and_ellipsis);
    TW_RUN(count_formatting);
    TW_RUN(welcome_banner);
    TW_RUN(view_input_box_and_status);
    TW_RUN(view_placeholder_and_wrapping);
    TW_RUN(view_menu_activity_and_hint);
    TW_RUN(compact_numbers_and_easing);
    TW_RUN(spinner_cycles);
    TW_RUN(progress_bar_has_sub_cell_steps);
    TW_RUN(word_diff_marks_changed_words);
    TW_RUN(canvas_blit_and_row_compare);
    TW_RUN(view_progress_threads_and_toast);
    TW_RUN(result_card_counts_up);
    TW_RUN(welcome_shine_keeps_text);
    return tw_test_summary();
}
