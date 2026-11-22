# 0002. An inline live region instead of the alternate screen

Status: accepted (v0.5.0)

## Context

Full-screen programs switch to the terminal's alternate screen; when they
exit, everything they showed disappears. TwinLine's output is mostly results
the user wants to keep reading or copy: reports, clusters, search results.

## Decision

TwinLine stays on the normal screen. Results are printed above a small live
region (the input box, the command menu, the progress view and the status
bar), which is erased and redrawn in place. Finished output scrolls into the
terminal's scrollback like any other program's.

## Consequences

- Results stay visible after TwinLine exits and can be scrolled and copied
  with the terminal's own tools.
- The live region must be kept short and must be erased exactly: the
  renderer tracks how many rows it drew and where the cursor is.
- Background results are printed above the live region without disturbing
  what the user is typing.
