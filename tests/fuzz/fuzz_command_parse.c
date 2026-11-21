/*
 * Fuzz target: splitting command lines into name, argument and force flag.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cmd/commands.h"
#include "fuzz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *line = tw_fuzz_cstring(data, size);
    if (line == NULL)
        return 0;

    size_t len = strlen(line);
    tw_parsed_command cmd;
    if (tw_command_parse(line, &cmd)) {
        /* Both parts point inside the original buffer. */
        TW_FUZZ_ASSERT(cmd.name >= line && cmd.name < line + len);
        TW_FUZZ_ASSERT(cmd.arg >= line && cmd.arg <= line + len);

        /* The name is a non-empty word without blanks. */
        TW_FUZZ_ASSERT(cmd.name[0] != '\0');
        TW_FUZZ_ASSERT(strpbrk(cmd.name, " \t") == NULL);

        /* The argument has no leading blanks. */
        TW_FUZZ_ASSERT(cmd.arg[0] != ' ' && cmd.arg[0] != '\t');
    } else {
        /* Only blank lines are rejected. */
        TW_FUZZ_ASSERT(strspn(line, " \t") == len);
    }

    free(line);
    return 0;
}
