/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/widgets.h"

#include <math.h>
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

void tw_format_compact(unsigned long long value, char *buf, size_t size)
{
    static const char *const SUFFIX[] = {"", "K", "M", "B", "T"};
    double v = (double)value;
    size_t unit = 0;
    while (v >= 1000.0 && unit < 4) {
        v /= 1000.0;
        unit++;
    }
    if (unit == 0)
        snprintf(buf, size, "%llu", value);
    else if (v < 10.0)
        snprintf(buf, size, "%.1f%s", v, SUFFIX[unit]);
    else
        snprintf(buf, size, "%.0f%s", v, SUFFIX[unit]);
}

/* ------------------------------------------------------------- animation */

double tw_ease_out(double t)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    double u = 1.0 - t;
    return 1.0 - u * u * u;
}

const char *tw_spinner_frame(double t)
{
    static const char *const FRAMES[] = {"✶", "✸", "✹", "✺", "✹", "✷"};
    if (t < 0.0)
        t = 0.0;
    size_t frame = (size_t)(t * 10.0) % (sizeof FRAMES / sizeof FRAMES[0]);
    return FRAMES[frame];
}

void tw_draw_progress(tw_canvas *canvas, int x, int y, int width, double fraction, tw_rgb from,
                      tw_rgb to, tw_rgb track)
{
    static const char *const EIGHTHS[] = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
    if (width <= 0)
        return;
    if (fraction < 0.0)
        fraction = 0.0;
    if (fraction > 1.0)
        fraction = 1.0;

    int eighths = (int)(fraction * (double)width * 8.0 + 0.5);
    int full = eighths / 8;
    int part = eighths % 8;
    for (int i = 0; i < width; i++) {
        double t = width > 1 ? (double)i / (double)(width - 1) : 0.0;
        tw_style fill = tw_style_fg(tw_rgb_mix(from, to, t), 0);
        if (i < full)
            tw_canvas_put(canvas, x + i, y, "█", fill);
        else if (i == full && part > 0)
            tw_canvas_put(canvas, x + i, y, EIGHTHS[part], fill);
        else
            tw_canvas_put(canvas, x + i, y, "░", tw_style_fg(track, 0));
    }
}

/* Comparison key of a word: its letters and digits, case-folded. */
static void word_key(const char *word, size_t len, char *key, size_t key_size)
{
    size_t out = 0;
    size_t i = 0;
    while (i < len && out + 5 < key_size) {
        uint32_t cp;
        size_t n = tw_utf8_decode((const unsigned char *)word + i, &cp);
        if (i + n > len)
            break;
        if (tw_utf8_is_word(cp)) {
            unsigned char buf[4];
            size_t m = tw_utf8_encode(tw_utf8_fold(cp, TW_FOLD_DEFAULT), buf);
            memcpy(key + out, buf, m);
            out += m;
        }
        i += n;
    }
    key[out] = '\0';
}

#define DIFF_WORDS 64
#define KEY_SIZE   48

static size_t split_words(const char *text, char keys[][KEY_SIZE])
{
    size_t count = 0;
    const char *p = text;
    while (*p != '\0' && count < DIFF_WORDS) {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;
        const char *start = p;
        while (*p != '\0' && *p != ' ')
            p++;
        word_key(start, (size_t)(p - start), keys[count], KEY_SIZE);
        count++;
    }
    return count;
}

void tw_word_diff(const char *a, const char *b, uint64_t *changed_a, uint64_t *changed_b)
{
    static char keys_a[DIFF_WORDS][KEY_SIZE];
    static char keys_b[DIFF_WORDS][KEY_SIZE];
    static unsigned char lcs[DIFF_WORDS + 1][DIFF_WORDS + 1];
    /* The static buffers keep the stack small; callers hold the UI lock. */
    size_t na = split_words(a, keys_a);
    size_t nb = split_words(b, keys_b);

    for (size_t i = na + 1; i-- > 0;) {
        for (size_t j = nb + 1; j-- > 0;) {
            if (i == na || j == nb)
                lcs[i][j] = 0;
            else if (strcmp(keys_a[i], keys_b[j]) == 0)
                lcs[i][j] = (unsigned char)(lcs[i + 1][j + 1] + 1);
            else
                lcs[i][j] = lcs[i + 1][j] > lcs[i][j + 1] ? lcs[i + 1][j] : lcs[i][j + 1];
        }
    }

    *changed_a = 0;
    *changed_b = 0;
    size_t i = 0;
    size_t j = 0;
    while (i < na || j < nb) {
        if (i < na && j < nb && strcmp(keys_a[i], keys_b[j]) == 0) {
            i++;
            j++;
        } else if (j == nb || (i < na && lcs[i + 1][j] >= lcs[i][j + 1])) {
            *changed_a |= (uint64_t)1 << i;
            i++;
        } else {
            *changed_b |= (uint64_t)1 << j;
            j++;
        }
    }
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
        if (w->shine >= 0.0) {
            /* A soft band of light runs over the logo, tilted with the rows. */
            tw_rgb white = tw_rgb_hex(0xFFFFFF);
            for (int i = 0; i < LOGO_ROWS; i++) {
                for (int col = 0; col < LOGO_WIDTH; col++) {
                    int x = 3 + col;
                    double d = fabs((double)col - (w->shine - (double)i * 1.5));
                    double glow = 1.0 - d / 5.0;
                    tw_cell *cell = &out->cells[(size_t)(y + i) * (size_t)out->width + (size_t)x];
                    if (glow > 0.0 && (cell->style.flags & TW_STYLE_FG))
                        cell->style.fg = tw_rgb_mix(cell->style.fg, white, glow * 0.85);
                }
            }
        }
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
    const char *mode = view->mode;
    if (mode == NULL)
        mode = view->exhaustive ? "exhaustive" : "pruned";
    snprintf(info, sizeof info, " · %s line%s · %zu thread%s · %s%s", count,
             view->lines == 1 ? "" : "s", view->threads, view->threads == 1 ? "" : "s", mode,
             view->turkish ? " · turkish" : "");
    tw_draw_text(canvas, x, y, canvas->width - x - 1, info, muted);
}

#define PANEL_MAX_THREADS 16

/* Rows the activity area needs. */
static int activity_rows(const tw_view *view)
{
    if (view->progress == NULL)
        return view->activity ? 1 : 0;
    const tw_activity *a = view->progress;
    int rows = 2; /* status line and progress bar */
    if (a->thread_count > 0) {
        rows += (int)(a->thread_count < PANEL_MAX_THREADS ? a->thread_count : PANEL_MAX_THREADS);
        if (a->thread_count > PANEL_MAX_THREADS)
            rows++;
    }
    return rows;
}

/* "Analyzing…" with a band of light sweeping over it. */
static int draw_shimmer(tw_canvas *canvas, int x, int y, const char *text, const tw_view *view)
{
    const tw_theme *theme = view->theme;
    if (!view->animate)
        return tw_canvas_put(canvas, x, y, text, tw_style_fg(theme->accent2, TW_STYLE_BOLD));

    int len = (int)tw_utf8_str_width(text);
    double band = fmod(view->time * 14.0, (double)(len + 10)) - 5.0;
    tw_rgb white = tw_rgb_hex(0xFFFFFF);
    const unsigned char *p = (const unsigned char *)text;
    int i = 0;
    char glyph[5];
    while (*p != '\0') {
        uint32_t cp;
        size_t n = tw_utf8_decode(p, &cp);
        double glow = 1.0 - fabs((double)i - band) / 3.0;
        tw_rgb color = glow > 0.0 ? tw_rgb_mix(theme->accent2, white, glow * 0.8) : theme->accent2;
        memcpy(glyph, p, n);
        glyph[n] = '\0';
        x = tw_canvas_put(canvas, x, y, glyph, tw_style_fg(color, TW_STYLE_BOLD));
        p += n;
        i++;
    }
    return x;
}

static int draw_activity(tw_canvas *canvas, const tw_view *view, int y)
{
    const tw_theme *theme = view->theme;
    tw_style muted = tw_style_fg(theme->muted, 0);
    int width = canvas->width;

    if (view->progress == NULL) {
        int x = tw_canvas_put(canvas, 1, y, "✶ ", tw_style_fg(theme->accent2, TW_STYLE_BOLD));
        tw_draw_text(canvas, x, y, width - x - 1, view->activity, muted);
        return y + 1;
    }

    const tw_activity *a = view->progress;
    double wave = view->animate ? (sin(view->time * 4.0) + 1.0) / 2.0 : 0.0;
    tw_rgb spin_color = tw_rgb_mix(theme->accent, theme->accent2, wave);
    const char *spinner = view->animate ? tw_spinner_frame(view->time) : "✶";
    int x = tw_canvas_put(canvas, 1, y, spinner, tw_style_fg(spin_color, TW_STYLE_BOLD));
    x = tw_canvas_put(canvas, x, y, " ", tw_style_plain());

    char label[64];
    snprintf(label, sizeof label, "%s…", a->cancelling ? "Cancelling" : a->label);
    x = draw_shimmer(canvas, x, y, label, view);

    char done[16];
    char total[16];
    char info[160];
    tw_format_compact(a->done, done, sizeof done);
    tw_format_compact(a->total, total, sizeof total);
    snprintf(info, sizeof info, " %.1f s · %.0f%% · %s / %s pairs · %zu thread%s%s", a->elapsed,
             100.0 * a->fraction, done, total, a->threads, a->threads == 1 ? "" : "s",
             a->cancellable && !a->cancelling ? " · esc to cancel" : "");
    tw_draw_text(canvas, x, y, width - x - 1, info, muted);
    y++;

    int bar = width - 12 < 48 ? width - 12 : 48;
    tw_draw_progress(canvas, 3, y, bar, a->fraction, theme->accent, theme->accent2, theme->border);
    char pct[16];
    snprintf(pct, sizeof pct, "%3.0f%%", 100.0 * a->fraction);
    tw_canvas_put(canvas, 3 + bar + 1, y, pct, tw_style_fg(theme->accent2, TW_STYLE_BOLD));
    y++;

    size_t shown = a->thread_count < PANEL_MAX_THREADS ? a->thread_count : PANEL_MAX_THREADS;
    int thread_bar = bar - 10 > 8 ? bar - 10 : 8;
    for (size_t t = 0; t < shown; t++) {
        char name[16];
        snprintf(name, sizeof name, "T%-2zu", t + 1);
        tw_canvas_put(canvas, 5, y, name, muted);
        double pos = shown > 1 ? (double)t / (double)(shown - 1) : 0.0;
        tw_rgb hue = tw_rgb_mix(theme->accent, theme->accent2, pos);
        tw_draw_progress(canvas, 9, y, thread_bar, a->thread_fractions[t], hue, hue, theme->border);
        snprintf(pct, sizeof pct, "%3.0f%%", 100.0 * a->thread_fractions[t]);
        tw_canvas_put(canvas, 9 + thread_bar + 1, y, pct, muted);
        y++;
    }
    if (a->thread_count > shown) {
        char more[48];
        snprintf(more, sizeof more, "… %zu more threads", a->thread_count - shown);
        tw_canvas_put(canvas, 5, y++, more, muted);
    }
    return y;
}

static void draw_toast(tw_canvas *canvas, const tw_view *view)
{
    const tw_theme *theme = view->theme;
    static const char *const ICONS[] = {"✔ ", "▲ ", "✶ "};
    tw_rgb colors[] = {theme->success, theme->warning, theme->accent2};
    int kind = (int)view->toast_kind;
    if (kind < 0 || kind > 2)
        kind = 2;

    double age = view->toast_age;
    int text_width = (int)tw_utf8_str_width(view->toast) + 2;
    int slide = 0;
    double fade = 0.0;
    if (view->animate) {
        slide = (int)((1.0 - tw_ease_out(age / 0.25)) * (double)(text_width + 2));
        double left = TW_TOAST_SECONDS - age;
        if (left < 0.6)
            fade = 1.0 - (left > 0.0 ? left : 0.0) / 0.6;
    }
    int x = canvas->width - 2 - text_width + slide;
    if (x < 1)
        x = 1;
    tw_rgb icon_color = tw_rgb_mix(colors[kind], theme->muted, fade);
    x = tw_canvas_put(canvas, x, 0, ICONS[kind], tw_style_fg(icon_color, TW_STYLE_BOLD));
    tw_canvas_put(canvas, x, 0, view->toast,
                  fade > 0.5 ? tw_style_fg(theme->muted, 0) : tw_style_plain());
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

    /* The first row separates the live region from the output above; toasts
     * appear at its right end. */
    int height = 1 + activity_rows(view) + input_rows + 2 + (int)shown + (more ? 1 : 0) +
                 (view->hint ? 1 : 0) + 1;
    if (!tw_canvas_init(out, width, height))
        return false;

    if (view->toast != NULL && view->toast_age < TW_TOAST_SECONDS)
        draw_toast(out, view);

    int y = 1;
    if (view->progress != NULL || view->activity != NULL)
        y = draw_activity(out, view, y);

    /* Input box: while busy the border breathes between the accent and the
     * border colour. */
    tw_rgb border_color = theme->accent;
    if (view->busy) {
        double breath = view->animate ? (sin(view->time * 3.0) + 1.0) / 2.0 : 0.0;
        border_color = tw_rgb_mix(theme->accent2, theme->border, breath * 0.7);
    }
    tw_style border = tw_style_fg(border_color, 0);
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

/* ------------------------------------------------------------ result card */

/* Draws `text` word by word, highlighting the words whose bit is set. */
static int draw_diff_words(tw_canvas *canvas, int x, int y, int max_x, const char *text,
                           uint64_t changed, tw_style normal, tw_style highlight)
{
    const char *p = text;
    size_t word = 0;
    char buf[256];
    while (*p != '\0' && x < max_x) {
        if (*p == ' ') {
            x = tw_canvas_put(canvas, x, y, " ", normal);
            p++;
            continue;
        }
        const char *start = p;
        while (*p != '\0' && *p != ' ')
            p++;
        size_t len = (size_t)(p - start);
        if (len >= sizeof buf)
            len = sizeof buf - 1;
        memcpy(buf, start, len);
        buf[len] = '\0';
        bool hit = word < 64 && ((changed >> word) & 1u);
        x = tw_canvas_put(canvas, x, y, buf, hit ? highlight : normal);
        word++;
    }
    return x;
}

#define CARD_SPIN_SECONDS 0.9 /* how long the count-up runs, for the spinner */

static unsigned long long counted(unsigned long long value, double progress)
{
    return (unsigned long long)((double)value * tw_ease_out(progress) + 0.5);
}

bool tw_card_render(const tw_result_card *card, tw_canvas *out)
{
    const tw_theme *theme = card->theme;
    const tw_analysis_result *r = card->result;
    bool has_pair = r->best_similarity > 0.0;
    int height = 1 + 3 + (has_pair ? 3 : 0);
    int width = card->width < 40 ? 40 : card->width;
    if (!tw_canvas_init(out, width, height))
        return false;

    tw_style muted = tw_style_fg(theme->muted, 0);
    tw_style bold = tw_style_plain();
    bold.flags = TW_STYLE_BOLD;
    double p = card->progress;
    char a[32];
    char b[32];
    char c[32];
    char line[256];

    /* Title. */
    int y = 1;
    /* The spinner glyph while the numbers count up, a check mark once done. */
    int x = p >= 1.0 ? tw_canvas_put(out, 0, y, "✔", tw_style_fg(theme->success, TW_STYLE_BOLD))
                     : tw_canvas_put(out, 0, y, tw_spinner_frame(p * CARD_SPIN_SECONDS),
                                     tw_style_fg(theme->accent2, TW_STYLE_BOLD));
    x = tw_canvas_put(out, x, y, " ", tw_style_plain());
    snprintf(line, sizeof line, "Analysis finished in %.2f s with %zu thread%s", r->seconds,
             r->threads, r->threads == 1 ? "" : "s");
    tw_draw_text(out, x, y, width - x - 1, line, bold);
    y++;

    /* Sizes. */
    x = tw_canvas_put(out, 2, y, "⎿ ", muted);
    tw_format_count((size_t)counted(r->lines_total, p), a, sizeof a);
    tw_format_count((size_t)counted(r->pairs, p), b, sizeof b);
    tw_format_count((size_t)counted(r->compared, p), c, sizeof c);
    char d[32];
    tw_format_count((size_t)counted(r->unique_lines, p), d, sizeof d);
    if (r->strategy == TW_STRATEGY_EXACT)
        snprintf(line, sizeof line, "%s lines · %s distinct · exact strategy", a, d);
    else if (r->strategy == TW_STRATEGY_MINHASH)
        snprintf(line, sizeof line, "%s lines · %s distinct · %s candidates (minhash)", a, d, c);
    else if (r->exhaustive)
        snprintf(line, sizeof line, "%s lines · %s line pairs · mean J %.4f", a, b,
                 r->mean_similarity * tw_ease_out(p));
    else
        snprintf(line, sizeof line, "%s lines · %s line pairs · %s compared", a, b, c);
    tw_draw_text(out, x, y, width - x - 1, line, muted);
    y++;

    /* Counts. */
    x = tw_canvas_put(out, 4, y, "● ", tw_style_fg(theme->success, TW_STYLE_BOLD));
    tw_format_count((size_t)counted(r->identical, p), a, sizeof a);
    x = tw_canvas_put(out, x, y, a, tw_style_fg(theme->success, TW_STYLE_BOLD));
    x = tw_canvas_put(out, x, y, " identical pairs    ", tw_style_plain());
    if (r->strategy == TW_STRATEGY_EXACT) {
        tw_draw_text(out, x, y, width - x - 1, "near duplicates not searched", muted);
        return true;
    }
    x = tw_canvas_put(out, x, y, "● ", tw_style_fg(theme->accent, TW_STYLE_BOLD));
    tw_format_count((size_t)counted(r->near, p), b, sizeof b);
    x = tw_canvas_put(out, x, y, b, tw_style_fg(theme->accent, TW_STYLE_BOLD));
    snprintf(line, sizeof line, " near duplicates (J ≥ %.2f)", r->threshold);
    tw_draw_text(out, x, y, width - x - 1, line, tw_style_plain());
    y++;

    if (!has_pair)
        return true;

    /* Most similar pair with a similarity badge and a word diff. */
    x = tw_canvas_put(out, 4, y, "Most similar pair ", muted);
    snprintf(line, sizeof line, " J %.2f ", r->best_similarity * tw_ease_out(p));
    tw_style badge = tw_style_fg(tw_rgb_hex(0x111118), TW_STYLE_BOLD);
    int bx = x;
    x = tw_canvas_put(out, x, y, line, badge);
    tw_canvas_paint_bg(out, bx, y, x - bx, theme->accent2);
    y++;

    uint64_t changed_a = 0;
    uint64_t changed_b = 0;
    tw_word_diff(r->best_a_text, r->best_b_text, &changed_a, &changed_b);
    tw_style del = tw_style_fg(theme->error, TW_STYLE_BOLD | TW_STYLE_UNDERLINE);
    tw_style ins = tw_style_fg(theme->success, TW_STYLE_BOLD | TW_STYLE_UNDERLINE);

    snprintf(line, sizeof line, "%7zu", r->best_a);
    x = tw_canvas_put(out, 2, y, line, muted);
    draw_diff_words(out, x + 2, y, width - 1, r->best_a_text, changed_a, tw_style_plain(), del);
    y++;
    snprintf(line, sizeof line, "%7zu", r->best_b);
    x = tw_canvas_put(out, 2, y, line, muted);
    draw_diff_words(out, x + 2, y, width - 1, r->best_b_text, changed_b, tw_style_plain(), ins);
    return true;
}
