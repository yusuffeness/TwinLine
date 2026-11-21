/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/widgets.h"

#include <stdio.h>
#include <string.h>

#include "twinline/utf8.h"

/* "TwinLine" in the ANSI Shadow figlet font. */
static const char *const LOGO[] = {
    "████████╗██╗    ██╗██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗",
    "╚══██╔══╝██║    ██║██║████╗  ██║██║     ██║████╗  ██║██╔════╝",
    "   ██║   ██║ █╗ ██║██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ",
    "   ██║   ██║███╗██║██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ",
    "   ██║   ╚███╔███╔╝██║██║ ╚████║███████╗██║██║ ╚████║███████╗",
    "   ╚═╝    ╚══╝╚══╝ ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝",
};
#define LOGO_ROWS  6
#define LOGO_WIDTH 61

/* ------------------------------------------------------------- primitives */

static void put_repeat(tw_canvas *canvas, int x, int y, int count, const char *glyph,
                       tw_style style)
{
    for (int i = 0; i < count; i++)
        tw_canvas_put(canvas, x + i, y, glyph, style);
}

void tw_draw_box(tw_canvas *canvas, int x, int y, int width, int height, tw_style border)
{
    if (width < 2 || height < 2)
        return;
    tw_canvas_put(canvas, x, y, "╭", border);
    put_repeat(canvas, x + 1, y, width - 2, "─", border);
    tw_canvas_put(canvas, x + width - 1, y, "╮", border);
    for (int row = y + 1; row < y + height - 1; row++) {
        tw_canvas_put(canvas, x, row, "│", border);
        tw_canvas_put(canvas, x + width - 1, row, "│", border);
    }
    tw_canvas_put(canvas, x, y + height - 1, "╰", border);
    put_repeat(canvas, x + 1, y + height - 1, width - 2, "─", border);
    tw_canvas_put(canvas, x + width - 1, y + height - 1, "╯", border);
}

int tw_draw_text(tw_canvas *canvas, int x, int y, int max_width, const char *text, tw_style style)
{
    if (max_width <= 0)
        return x;
    if ((int)tw_utf8_str_width(text) <= max_width)
        return tw_canvas_put(canvas, x, y, text, style);

    /* Draw characters while one column stays free for the ellipsis. */
    const unsigned char *p = (const unsigned char *)text;
    int col = x;
    char glyph[5];
    while (*p != '\0') {
        uint32_t cp;
        size_t len = tw_utf8_decode(p, &cp);
        int w = tw_utf8_width(cp);
        if (col + w > x + max_width - 1)
            break;
        memcpy(glyph, p, len);
        glyph[len] = '\0';
        col = tw_canvas_put(canvas, col, y, glyph, style);
        p += len;
    }
    return tw_canvas_put(canvas, col, y, "…", style);
}

void tw_draw_gradient(tw_canvas *canvas, int x, int y, const char *text, tw_rgb from, tw_rgb to,
                      int x0, int span, uint8_t flags)
{
    const unsigned char *p = (const unsigned char *)text;
    char glyph[5];
    while (*p != '\0') {
        uint32_t cp;
        size_t len = tw_utf8_decode(p, &cp);
        double t = span > 1 ? (double)(x - x0) / (double)(span - 1) : 0.0;
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        memcpy(glyph, p, len);
        glyph[len] = '\0';
        x = tw_canvas_put(canvas, x, y, glyph, tw_style_fg(tw_rgb_mix(from, to, t), flags));
        p += len;
    }
}

void tw_format_count(size_t value, char *buf, size_t size)
{
    char digits[32];
    int n = snprintf(digits, sizeof digits, "%zu", value);
    if (n <= 0 || size == 0) {
        if (size > 0)
            buf[0] = '\0';
        return;
    }
    size_t out = 0;
    for (int i = 0; i < n && out + 1 < size; i++) {
        if (i > 0 && (n - i) % 3 == 0 && out + 2 < size)
            buf[out++] = ',';
        buf[out++] = digits[i];
    }
    buf[out] = '\0';
}

/* Draws `text` right-aligned to `max_width`, keeping its end ("…tail"). */
static void draw_tail(tw_canvas *canvas, int x, int y, int max_width, const char *text,
                      tw_style style)
{
    const char *p = text;
    while ((int)tw_utf8_str_width(p) > max_width - 1 && *p != '\0') {
        uint32_t cp;
        p += tw_utf8_decode((const unsigned char *)p, &cp);
    }
    if (p != text)
        x = tw_canvas_put(canvas, x, y, "…", style);
    tw_canvas_put(canvas, x, y, p, style);
}

/* ---------------------------------------------------------------- welcome */

bool tw_welcome_render(const tw_welcome *w, tw_canvas *out)
{
    const tw_theme *theme = w->theme;
    bool big_logo = w->width >= LOGO_WIDTH + 6;
    int box_width = LOGO_WIDTH + 6;
    if (!big_logo)
        box_width = w->width < 64 ? w->width : 64;
    if (box_width < 24)
        box_width = w->width > 0 ? w->width : 24;
    int logo_rows = big_logo ? LOGO_ROWS + 1 : 0;
    int height = 2 + 1 + logo_rows + 3 + 1;

    if (!tw_canvas_init(out, box_width, height))
        return false;

    tw_style border = tw_style_fg(theme->accent, 0);
    tw_style muted = tw_style_fg(theme->muted, 0);
    tw_draw_box(out, 0, 0, box_width, height, border);

    int y = 2;
    if (big_logo) {
        for (int i = 0; i < LOGO_ROWS; i++)
            tw_draw_gradient(out, 3, y + i, LOGO[i], theme->accent, theme->accent2, 3, LOGO_WIDTH,
                             TW_STYLE_BOLD);
        y += logo_rows;
    }

    int inner = box_width - 4;
    int x = tw_canvas_put(out, 2, y, "✻ ", tw_style_fg(theme->accent, TW_STYLE_BOLD));
    x = tw_canvas_put(out, x, y, "Welcome to ", tw_style_plain());
    x = tw_canvas_put(out, x, y, "TwinLine", tw_style_fg(theme->accent, TW_STYLE_BOLD));
    char version[32];
    snprintf(version, sizeof version, " v%s", w->version);
    tw_canvas_put(out, x, y, version, muted);

    tw_draw_text(out, 4, y + 1, inner - 2, "/help for commands · ? for shortcuts · Tab to complete",
                 muted);
    int cx = tw_canvas_put(out, 4, y + 2, "cwd: ", muted);
    draw_tail(out, cx, y + 2, inner - 2 - 5, w->cwd ? w->cwd : "", muted);
    return true;
}

/* ----------------------------------------------------------- live region */

/*
 * Lays the input out in rows of `cols` columns. When `canvas` is not NULL the
 * text is drawn at (x0, y0). Returns the number of rows and stores the
 * cursor position.
 */
static int layout_input(const char *input, size_t cursor, int cols, tw_canvas *canvas, int x0,
                        int y0, int *cursor_row, int *cursor_col)
{
    const unsigned char *start = (const unsigned char *)input;
    const unsigned char *p = start;
    int row = 0;
    int col = 0;
    bool cursor_set = false;
    char glyph[5];

    while (*p != '\0') {
        size_t offset = (size_t)(p - start);
        uint32_t cp;
        size_t len = tw_utf8_decode(p, &cp);
        int w = tw_utf8_width(cp);
        if (w > 0 && col + w > cols) {
            row++;
            col = 0;
        }
        if (!cursor_set && offset >= cursor) {
            *cursor_row = row;
            *cursor_col = col;
            cursor_set = true;
        }
        if (canvas != NULL && w > 0) {
            memcpy(glyph, p, len);
            glyph[len] = '\0';
            tw_canvas_put(canvas, x0 + col, y0 + row, glyph, tw_style_plain());
        }
        col += w;
        p += len;
    }

    if (!cursor_set) {
        if (col >= cols) {
            row++;
            col = 0;
        }
        *cursor_row = row;
        *cursor_col = col;
    }
    return (*cursor_row > row ? *cursor_row : row) + 1;
}

static int max_label_width(const tw_view *view, size_t first, size_t last)
{
    int widest = 0;
    for (size_t i = first; i < last; i++) {
        int w = (int)tw_utf8_str_width(view->items[i].label);
        if (w > widest)
            widest = w;
    }
    return widest > 22 ? 22 : widest;
}

static void draw_menu_item(tw_canvas *canvas, const tw_view *view, size_t index, int y,
                           int label_width)
{
    const tw_theme *theme = view->theme;
    const tw_menu_item *item = &view->items[index];
    bool selected = index == view->selected;

    if (selected)
        tw_canvas_paint_bg(canvas, 1, y, canvas->width - 2, theme->selection);

    /* Label, with the characters that matched the filter highlighted. */
    const unsigned char *p = (const unsigned char *)item->label;
    const unsigned char *start = p;
    int x = 3;
    char glyph[5];
    while (*p != '\0' && x < 3 + label_width) {
        size_t offset = (size_t)(p - start);
        uint32_t cp;
        size_t len = tw_utf8_decode(p, &cp);
        bool matched = offset < 64 && (item->match_bits >> offset) & 1u;
        tw_style style =
            (matched || selected) ? tw_style_fg(theme->accent, TW_STYLE_BOLD) : tw_style_plain();
        if (matched && !selected)
            style.flags |= TW_STYLE_UNDERLINE;
        memcpy(glyph, p, len);
        glyph[len] = '\0';
        x = tw_canvas_put(canvas, x, y, glyph, style);
        p += len;
    }
    if (item->detail != NULL) {
        int dx = 3 + label_width + 2;
        tw_draw_text(canvas, dx, y, canvas->width - dx - 2, item->detail,
                     tw_style_fg(theme->muted, 0));
    }
    if (selected)
        tw_canvas_paint_bg(canvas, 1, y, canvas->width - 2, theme->selection);
}

static void draw_status(tw_canvas *canvas, const tw_view *view, int y)
{
    const tw_theme *theme = view->theme;
    tw_style muted = tw_style_fg(theme->muted, 0);
    int x = tw_canvas_put(canvas, 1, y, "▸ ", tw_style_fg(theme->accent2, TW_STYLE_BOLD));

    if (view->file != NULL)
        x = tw_draw_text(canvas, x, y, 32, view->file, tw_style_plain());
    else
        x = tw_canvas_put(canvas, x, y, "no file", muted);
    if (view->modified)
        x = tw_canvas_put(canvas, x, y, " ● modified", tw_style_fg(theme->warning, 0));

    char count[32];
    char info[160];
    tw_format_count(view->lines, count, sizeof count);
    snprintf(info, sizeof info, " · %s line%s · %zu thread%s · %s%s", count,
             view->lines == 1 ? "" : "s", view->threads, view->threads == 1 ? "" : "s",
             view->exhaustive ? "exhaustive" : "pruned", view->turkish ? " · turkish" : "");
    tw_draw_text(canvas, x, y, canvas->width - x - 1, info, muted);
}

bool tw_view_render(const tw_view *view, tw_canvas *out, int *cursor_x, int *cursor_y)
{
    const tw_theme *theme = view->theme;
    int width = view->width < 24 ? 24 : view->width;
    int text_cols = width - 6;

    int cursor_row = 0;
    int cursor_col = 0;
    int input_rows =
        layout_input(view->input, view->cursor, text_cols, NULL, 0, 0, &cursor_row, &cursor_col);

    size_t first = 0;
    size_t shown = view->item_count;
    if (shown > TW_MENU_VISIBLE) {
        shown = TW_MENU_VISIBLE;
        if (view->selected >= TW_MENU_VISIBLE)
            first = view->selected - TW_MENU_VISIBLE + 1;
    }
    bool more = first + shown < view->item_count;

    /* A blank row first separates the live region from the output above. */
    int height = 1 + (view->activity ? 1 : 0) + input_rows + 2 + (int)shown + (more ? 1 : 0) +
                 (view->hint ? 1 : 0) + 1;
    if (!tw_canvas_init(out, width, height))
        return false;

    int y = 1;
    if (view->activity != NULL) {
        int x = tw_canvas_put(out, 1, y, "✶ ", tw_style_fg(theme->accent2, TW_STYLE_BOLD));
        tw_draw_text(out, x, y, width - x - 1, view->activity, tw_style_fg(theme->muted, 0));
        y++;
    }

    /* Input box. */
    tw_style border = tw_style_fg(view->busy ? theme->accent2 : theme->accent, 0);
    tw_draw_box(out, 0, y, width, input_rows + 2, border);
    if (view->title != NULL) {
        int x = tw_canvas_put(out, 2, y, " ", border);
        x = tw_draw_text(out, x, y, width - 6, view->title,
                         tw_style_fg(theme->accent, TW_STYLE_BOLD));
        tw_canvas_put(out, x, y, " ", border);
    }
    tw_canvas_put(out, 2, y + 1, ">", tw_style_fg(theme->accent, TW_STYLE_BOLD));
    if (view->input[0] == '\0' && view->title == NULL)
        tw_draw_text(out, 4, y + 1, text_cols, "Type a command, or / for the menu",
                     tw_style_fg(theme->muted, TW_STYLE_ITALIC));
    else
        layout_input(view->input, view->cursor, text_cols, out, 4, y + 1, &cursor_row, &cursor_col);
    *cursor_x = 4 + cursor_col;
    *cursor_y = y + 1 + cursor_row;
    y += input_rows + 2;

    /* Menu. */
    if (shown > 0) {
        int label_width = max_label_width(view, first, first + shown);
        for (size_t i = first; i < first + shown; i++)
            draw_menu_item(out, view, i, y++, label_width);
        if (more) {
            char note[48];
            snprintf(note, sizeof note, "↓ %zu more", view->item_count - first - shown);
            tw_canvas_put(out, 3, y++, note, tw_style_fg(theme->muted, 0));
        }
    }

    if (view->hint != NULL) {
        tw_draw_text(out, 2, y, width - 3, view->hint, tw_style_fg(theme->muted, TW_STYLE_ITALIC));
        y++;
    }

    draw_status(out, view, y);
    return true;
}
