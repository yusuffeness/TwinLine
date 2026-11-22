/*
 * Unit tests for src/cmd/config.c
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "../tw_test.h"
#include "cmd/config.h"

static bool parse(const char *text, tw_config *c, char *msg, size_t size)
{
    tw_config_default(c);
    return tw_config_parse(text, "config.toml", c, msg, size);
}

TW_TEST(defaults)
{
    tw_config c;
    tw_config_default(&c);
    TW_CHECK(c.analysis.threshold == TW_DEFAULT_THRESHOLD);
    TW_CHECK_EQ_STR(c.theme, "dark");
    TW_CHECK(c.animations && c.color && c.autosave && c.watch);
    TW_CHECK_EQ_INT((int)c.autosave_interval, TW_AUTOSAVE_DEFAULT_INTERVAL);
}

TW_TEST(full_file)
{
    static const char text[] = "# TwinLine settings\r\n"
                               "\n"
                               "[analysis]\n"
                               "threshold = 0.85      # stricter\n"
                               "threads   = 8\n"
                               "strategy  = 'minhash'\n"
                               "exhaustive = true\n"
                               "fold = \"turkish\"\n"
                               "ngram = 3\n"
                               "  [ ui ]  \n"
                               "theme = \"dracula\"\n"
                               "animations = false\n"
                               "color = false\n"
                               "[autosave]\n"
                               "enabled = false\n"
                               "interval = 1_200\n"
                               "[watch]\n"
                               "enabled = false\n";
    tw_config c;
    char msg[256] = "";
    TW_CHECK(parse(text, &c, msg, sizeof msg));
    TW_CHECK_EQ_STR(msg, "");
    TW_CHECK(c.analysis.threshold == 0.85);
    TW_CHECK_EQ_SIZE(c.analysis.threads, 8);
    TW_CHECK_EQ_INT(c.analysis.strategy, TW_STRATEGY_MINHASH);
    TW_CHECK(c.analysis.exhaustive);
    TW_CHECK_EQ_INT(c.analysis.fold, TW_FOLD_TURKISH);
    TW_CHECK_EQ_INT(c.analysis.ngram, 3);
    TW_CHECK_EQ_STR(c.theme, "dracula");
    TW_CHECK(!c.animations && !c.color && !c.autosave && !c.watch);
    TW_CHECK_EQ_INT((int)c.autosave_interval, 1200);

    /* threads = "auto" and an integer threshold work too. */
    TW_CHECK(parse("[analysis]\nthreads = \"auto\"\nthreshold = 1\n", &c, msg, sizeof msg));
    TW_CHECK_EQ_SIZE(c.analysis.threads, 0);
    TW_CHECK(c.analysis.threshold == 1.0);
}

/* Each bad file is rejected with the line it went wrong on. */
TW_TEST(errors_name_the_line)
{
    static const struct {
        const char *text;
        const char *message;
    } CASES[] = {
        {"[analysis]\ntreshold = 0.5\n", "config.toml:2: unknown key 'treshold' in [analysis]"},
        {"threshold = 0.5\n", "config.toml:1: 'threshold' must be inside a table"},
        {"[colours]\n", "config.toml:1: unknown table [colours]"},
        {"[analysis\n", "config.toml:1: expected a table name"},
        {"[analysis]\nthreshold 0.5\n", "config.toml:2: expected '=' after 'threshold'"},
        {"[analysis]\nthreshold = \n", "config.toml:2: expected a value"},
        {"[analysis]\nthreshold = 1.5\n", "config.toml:2: threshold must be a number above 0"},
        {"[analysis]\nthreshold = \"high\"\n", "config.toml:2: 'threshold' must be a number"},
        {"[analysis]\nngram = 2.5\n", "config.toml:2: 'ngram' must be an integer"},
        {"[analysis]\nstrategy = \"fast\n", "config.toml:2: unterminated string"},
        {"[analysis]\nstrategy = \"a\\qb\"\n", "config.toml:2: unsupported escape"},
        {"[analysis]\nngram = 2 3\n", "config.toml:2: unexpected text after the value of 'ngram'"},
        {"[analysis]\nngram = 2\nngram = 3\n", "config.toml:3: 'ngram' is set twice"},
        {"[analysis]\nthreads = \"many\"\n", "config.toml:2: 'threads' must be \"auto\""},
        {"[ui]\ntheme = \"neon\"\n", "config.toml:2: unknown theme 'neon'"},
        {"[ui]\nanimations = yes\n", "config.toml:2: expected a value"},
        {"[ui]\ncolor = 1\n", "config.toml:2: 'color' must be true or false"},
        {"[autosave]\ninterval = 0\n", "config.toml:2: 'interval' must be 1 to 3600 seconds"},
        {"[autosave]\nevery = 5\n", "config.toml:2: unknown key 'every' in [autosave]"},
        {"[watch]\nfast = true\n", "config.toml:2: unknown key 'fast' in [watch]"},
        {"[analysis]\nngram = 99999999999999999999\n", "config.toml:2: not a valid number"},
    };
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        tw_config c;
        char msg[256] = "";
        bool ok = parse(CASES[i].text, &c, msg, sizeof msg);
        TW_CHECK(!ok);
        if (strncmp(msg, CASES[i].message, strlen(CASES[i].message)) != 0) {
            printf("    case %zu: got \"%s\"\n", i, msg);
            TW_CHECK(false);
        }
    }
}

/* What tw_config_print() writes reads back as the same settings. */
TW_TEST(print_round_trips)
{
    tw_config c;
    char msg[256];
    TW_CHECK(parse("[analysis]\nthreshold = 0.8\nstrategy = \"exact\"\nngram = 1\n"
                   "[ui]\ntheme = \"light\"\n[autosave]\ninterval = 7\n",
                   &c, msg, sizeof msg));
    char *text = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&text, &len);
    TW_CHECK(out != NULL);
    tw_config_print(out, &c);
    fclose(out);

    tw_config back;
    TW_CHECK(parse(text, &back, msg, sizeof msg));
    TW_CHECK(back.analysis.threshold == 0.8);
    TW_CHECK_EQ_INT(back.analysis.strategy, TW_STRATEGY_EXACT);
    TW_CHECK_EQ_INT(back.analysis.ngram, 1);
    TW_CHECK_EQ_STR(back.theme, "light");
    TW_CHECK_EQ_INT((int)back.autosave_interval, 7);
    free(text);
}

TW_TEST(load_from_disk)
{
    char path[] = "/tmp/twinline-config-XXXXXX";
    int fd = mkstemp(path);
    TW_CHECK(fd >= 0);
    FILE *f = fdopen(fd, "w");
    TW_CHECK(f != NULL);
    fputs("[analysis]\nthreshold = 0.9\n", f);
    fclose(f);

    tw_config c;
    tw_config_default(&c);
    bool found = false;
    char msg[256];
    TW_CHECK(tw_config_load(path, &c, &found, msg, sizeof msg));
    TW_CHECK(found);
    TW_CHECK(c.analysis.threshold == 0.9);
    remove(path);

    /* A missing file is fine and changes nothing. */
    tw_config_default(&c);
    TW_CHECK(tw_config_load(path, &c, &found, msg, sizeof msg));
    TW_CHECK(!found);
    TW_CHECK(c.analysis.threshold == TW_DEFAULT_THRESHOLD);
}

TW_TEST(default_path)
{
    setenv("TWINLINE_CONFIG", "/etc/tw.toml", 1);
    char *p = tw_config_default_path();
    TW_CHECK_EQ_STR(p, "/etc/tw.toml");
    free(p);
    unsetenv("TWINLINE_CONFIG");
    setenv("XDG_CONFIG_HOME", "/home/u/.cfg", 1);
    p = tw_config_default_path();
    TW_CHECK_EQ_STR(p, "/home/u/.cfg/twinline/config.toml");
    free(p);
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", "/home/u", 1);
    p = tw_config_default_path();
    TW_CHECK_EQ_STR(p, "/home/u/.config/twinline/config.toml");
    free(p);
}

int main(void)
{
    puts("config");
    TW_RUN(defaults);
    TW_RUN(full_file);
    TW_RUN(errors_name_the_line);
    TW_RUN(print_round_trips);
    TW_RUN(load_from_disk);
    TW_RUN(default_path);
    return tw_test_summary();
}
