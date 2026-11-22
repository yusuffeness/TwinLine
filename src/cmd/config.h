/*
 * The configuration file, config.toml.
 *
 * Looked up at $TWINLINE_CONFIG, else $XDG_CONFIG_HOME/twinline/config.toml,
 * else ~/.config/twinline/config.toml. Values from the command line win over
 * the file, and the file wins over the built-in defaults.
 *
 *   [analysis]
 *   threshold  = 0.7        # 0 < x <= 1
 *   threads    = "auto"     # or 1 .. 256
 *   strategy   = "jaccard"  # jaccard, exact or minhash
 *   exhaustive = false
 *   fold       = "default"  # or "turkish"
 *   ngram      = 2          # 1 .. 4
 *
 *   [ui]
 *   theme      = "dark"     # dark, light, dracula or nord
 *   animations = true
 *   color      = true
 *
 *   [autosave]
 *   enabled    = true
 *   interval   = 30         # seconds, 1 .. 3600
 *
 *   [watch]
 *   enabled    = true       # notice changes made by other programs
 *
 * The file is a subset of TOML: tables, `key = value` pairs with strings,
 * integers, floats and booleans, and comments.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TWINLINE_CMD_CONFIG_H
#define TWINLINE_CMD_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "twinline/analysis.h"

typedef struct tw_config {
    tw_analysis_options analysis;
    char theme[16];
    bool animations;
    bool color;
    bool autosave;
    unsigned autosave_interval; /* seconds */
    bool watch;
} tw_config;

#define TW_AUTOSAVE_DEFAULT_INTERVAL 30
#define TW_AUTOSAVE_MAX_INTERVAL     3600

void tw_config_default(tw_config *config);

/*
 * Applies the TOML `text` to `config`. `name` is used in messages. On error
 * returns false with "name:line: problem" in `message`; `config` may then
 * hold some of the file's values.
 */
bool tw_config_parse(const char *text, const char *name, tw_config *config, char *message,
                     size_t size);

/*
 * Reads and applies the file at `path`. A missing file is not an error:
 * `*found` is set to false and `config` is unchanged.
 */
bool tw_config_load(const char *path, tw_config *config, bool *found, char *message, size_t size);

/* The default path of the configuration file (newly allocated), or NULL
 * when no home directory is known. */
char *tw_config_default_path(void);

/* Prints `config` as a complete TOML file. */
void tw_config_print(FILE *out, const tw_config *config);

#endif /* TWINLINE_CMD_CONFIG_H */
