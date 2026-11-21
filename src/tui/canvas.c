/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/canvas.h"

#include <stdlib.h>
#include <string.h>

#include "twinline/utf8.h"

static const tw_cell BLANK = {" ", 1, {{0, 0, 0}, {0, 0, 0}, 0}};

tw_style tw_style_plain(void)
{
    tw_style s;
    memset(&s, 0, sizeof s);
    return s;
}

tw_style tw_style_fg(tw_rgb fg, uint8_t flags)
{
    tw_style s = tw_style_plain();
    s.fg = fg;
    s.flags = (uint8_t)(flags | TW_STYLE_FG);
    return s;
}

bool tw_canvas_init(tw_canvas *canvas, int width, int height)
{
    canvas->width = width > 0 ? width : 0;
    canvas->height = height > 0 ? height : 0;
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    canvas->cells = malloc((count ? count : 1) * sizeof *canvas->cells);
    if (canvas->cells == NULL) {
        canvas->width = canvas->height = 0;
        return false;
    }
    for (size_t i = 0; i < count; i++)
        canvas->cells[i] = BLANK;
    return true;
}

void tw_canvas_free(tw_canvas *canvas)
{
    free(canvas->cells);
    canvas->cells = NULL;
    canvas->width = canvas->height = 0;
}

static tw_cell *cell_at(const tw_canvas *canvas, int x, int y)
{
    return &canvas->cells[(size_t)y * (size_t)canvas->width + (size_t)x];
}

int tw_canvas_put(tw_canvas *canvas, int x, int y, const char *text, tw_style style)
{
    if (y < 0 || y >= canvas->height)
        return x;

    const unsigned char *p = (const unsigned char *)text;
    while (*p != '\0') {
        uint32_t cp;
        size_t len = tw_utf8_decode(p, &cp);
        const unsigned char *bytes = p;
        p += len;

        if (cp == '\t') {
            cp = ' ';
            bytes = (const unsigned char *)" ";
            len = 1;
        }
        int width = tw_utf8_width(cp);
        if (width == 0)
            continue;
        if (x + width > canvas->width)
            break;
        if (x < 0) {
            x += width;
            continue;
        }

        tw_cell *cell = cell_at(canvas, x, y);
        if (cp == TW_UTF8_REPLACEMENT && len == 1) {
            memcpy(cell->glyph, "\xEF\xBF\xBD", 4); /* show U+FFFD for bad bytes */
        } else {
            memcpy(cell->glyph, bytes, len);
            cell->glyph[len] = '\0';
        }
        cell->width = (uint8_t)width;
        cell->style = style;
        if (width == 2) {
            tw_cell *right = cell_at(canvas, x + 1, y);
            right->glyph[0] = '\0';
            right->width = 0;
            right->style = style;
        }
        x += width;
    }
    return x;
}

void tw_canvas_paint_bg(tw_canvas *canvas, int x, int y, int count, tw_rgb bg)
{
    if (y < 0 || y >= canvas->height)
        return;
    for (int i = x < 0 ? 0 : x; i < x + count && i < canvas->width; i++) {
        tw_cell *cell = cell_at(canvas, i, y);
        cell->style.bg = bg;
        cell->style.flags |= TW_STYLE_BG;
    }
}

/* Number of cells in row `y` up to the last one that is not a plain blank. */
static int used_width(const tw_canvas *canvas, int y, bool keep_backgrounds)
{
    int end = canvas->width;
    while (end > 0) {
        const tw_cell *cell = cell_at(canvas, end - 1, y);
        bool blank = cell->width == 1 && cell->glyph[0] == ' ' && cell->glyph[1] == '\0';
        if (!blank || (keep_backgrounds && (cell->style.flags & TW_STYLE_BG)))
            break;
        end--;
    }
    return end;
}

void tw_canvas_row_text(const tw_canvas *canvas, int y, tw_strbuf *out)
{
    if (y < 0 || y >= canvas->height)
        return;
    int end = used_width(canvas, y, false);
    for (int x = 0; x < end; x++) {
        const tw_cell *cell = cell_at(canvas, x, y);
        if (cell->width != 0)
            tw_strbuf_append(out, cell->glyph);
    }
}

static bool same_style(tw_style a, tw_style b)
{
    if (a.flags != b.flags)
        return false;
    if ((a.flags & TW_STYLE_FG) && memcmp(&a.fg, &b.fg, sizeof a.fg) != 0)
        return false;
    if ((a.flags & TW_STYLE_BG) && memcmp(&a.bg, &b.bg, sizeof a.bg) != 0)
        return false;
    return true;
}

static void append_color(tw_strbuf *out, int base, tw_rgb color, tw_color_mode mode)
{
    if (mode == TW_COLOR_TRUE)
        tw_strbuf_appendf(out, ";%d;2;%u;%u;%u", base, color.r, color.g, color.b);
    else
        tw_strbuf_appendf(out, ";%d;5;%u", base, tw_rgb_to_256(color));
}

void tw_style_render(tw_style style, tw_color_mode mode, tw_strbuf *out)
{
    uint8_t flags = style.flags;
    if (mode == TW_COLOR_NONE)
        flags &= (uint8_t)~(TW_STYLE_FG | TW_STYLE_BG);
    if (flags == 0)
        return;

    tw_strbuf_append(out, "\x1b[0");
    if (flags & TW_STYLE_BOLD)
        tw_strbuf_append(out, ";1");
    if (flags & TW_STYLE_DIM)
        tw_strbuf_append(out, ";2");
    if (flags & TW_STYLE_ITALIC)
        tw_strbuf_append(out, ";3");
    if (flags & TW_STYLE_UNDERLINE)
        tw_strbuf_append(out, ";4");
    if (flags & TW_STYLE_FG)
        append_color(out, 38, style.fg, mode);
    if (flags & TW_STYLE_BG)
        append_color(out, 48, style.bg, mode);
    tw_strbuf_append(out, "m");
}

void tw_canvas_render_row(const tw_canvas *canvas, int y, tw_color_mode mode, tw_strbuf *out)
{
    if (y < 0 || y >= canvas->height)
        return;
    int end = used_width(canvas, y, true);
    tw_style current = tw_style_plain();
    bool styled = false;

    for (int x = 0; x < end; x++) {
        const tw_cell *cell = cell_at(canvas, x, y);
        if (cell->width == 0)
            continue;
        if (!same_style(cell->style, current)) {
            tw_strbuf_append(out, "\x1b[0m");
            tw_style_render(cell->style, mode, out);
            current = cell->style;
            styled = true;
        }
        tw_strbuf_append(out, cell->glyph);
    }
    if (styled)
        tw_strbuf_append(out, "\x1b[0m");
}
