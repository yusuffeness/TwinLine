# Security policy

## Supported versions

Security fixes are made for the latest release.

| Version | Supported |
| ------- | --------- |
| 1.x     | Yes       |
| < 1.0   | No        |

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it
privately through
[GitHub's private vulnerability reporting](https://github.com/yusuffeness/TwinLine/security/advisories/new),
with a description, the affected version and, if you can, the steps or the
input that reproduce it.

You should hear back within a week. Once the problem is confirmed, a fix is
prepared and released, and the report is credited unless you prefer
otherwise.

## Scope

TwinLine reads text files, a configuration file and its own recovery files,
and writes the files you save or export. Problems worth reporting include:

- memory errors or crashes on malformed input (text, configuration, recovery
  files, terminal input);
- writing outside the files the user asked for, or with wider permissions
  than intended (recovery files are created with mode 0600);
- escape sequences in a document reaching the terminal through the terminal
  interface (control characters are shown in caret notation, such as `^[`,
  and never sent; the plain interface and the subcommands print text
  unchanged, like `cat`).

Every release is built with all compiler warnings as errors and tested under
AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer and Valgrind;
the UTF-8 decoder, tokenizer, command parser and configuration reader are
fuzzed in CI, and CodeQL scans the code.
