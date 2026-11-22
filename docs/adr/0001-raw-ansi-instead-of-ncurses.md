# 0001. Raw ANSI output instead of ncurses

Status: accepted (v0.5.0)

## Context

The terminal interface needs 24-bit colour, rounded box characters, emoji and
wide characters, smooth animation, and a layout that behaves like an ordinary
command-line program: output scrolls, and the terminal's own scrollback keeps
working. It also has to be testable in CI without a terminal.

ncurses is the usual choice. It owns the whole screen, its colour model is a
fixed set of colour pairs, wide-character support depends on how the library
was built, and testing its output means driving a terminal.

## Decision

TwinLine writes ANSI escape sequences itself. Widgets draw into a canvas, a
grid of styled cells, and only changed rows are turned into escape sequences.
Colour support is detected from `COLORTERM`, `TERM` and `NO_COLOR` and
degrades from 24-bit to the 256-colour palette to none.

## Consequences

- No dependency beyond the C library and POSIX.
- Widgets are unit-tested by rendering into a canvas and comparing rows as
  text; an end-to-end test drives the real binary in a pseudo-terminal.
- TwinLine handles raw mode, resizes, signals and terminal restoration
  itself, and targets terminals that understand the common VT sequences
  (every current terminal emulator). Very old terminals are not supported.
