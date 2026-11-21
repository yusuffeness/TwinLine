/*
 * SPDX-License-Identifier: MIT
 */
#include "tui/theme.h"

#include <string.h>

#define RGB(hex)                                                                                   \
    {(uint8_t)(((hex) >> 16) & 0xFF), (uint8_t)(((hex) >> 8) & 0xFF), (uint8_t)((hex) & 0xFF)}

static const tw_theme THEMES[] = {
    {"dark", RGB(0xC084FC), RGB(0x22D3EE), RGB(0x4ADE80), RGB(0xFACC15), RGB(0xF87171),
     RGB(0x94A3B8), RGB(0x64748B), RGB(0x3B2A55)},
    {"light", RGB(0x7C3AED), RGB(0x0891B2), RGB(0x16A34A), RGB(0xCA8A04), RGB(0xDC2626),
     RGB(0x64748B), RGB(0xCBD5E1), RGB(0xEDE9FE)},
    {"dracula", RGB(0xBD93F9), RGB(0x8BE9FD), RGB(0x50FA7B), RGB(0xF1FA8C), RGB(0xFF5555),
     RGB(0x8B93C4), RGB(0x6272A4), RGB(0x44475A)},
    {"nord", RGB(0xB48EAD), RGB(0x88C0D0), RGB(0xA3BE8C), RGB(0xEBCB8B), RGB(0xBF616A),
     RGB(0x8C9AB5), RGB(0x4C566A), RGB(0x3B4252)},
};

#define THEME_COUNT (sizeof THEMES / sizeof THEMES[0])

const tw_theme *tw_theme_default(void)
{
    return &THEMES[0];
}

const tw_theme *tw_theme_find(const char *name)
{
    for (size_t i = 0; i < THEME_COUNT; i++) {
        if (strcmp(THEMES[i].name, name) == 0)
            return &THEMES[i];
    }
    return NULL;
}

size_t tw_theme_count(void)
{
    return THEME_COUNT;
}

const tw_theme *tw_theme_at(size_t index)
{
    return index < THEME_COUNT ? &THEMES[index] : NULL;
}
