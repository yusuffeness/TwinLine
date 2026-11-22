# 0005. Recovery files next to the original

Status: accepted (v0.9.0)

## Context

Autosave must protect unsaved work from crashes without touching the user's
file (only an explicit save may do that). The copy has to be found again
after a crash, by a new TwinLine process that knows only the file's name.

## Decision

Unsaved changes are written to `.NAME.twinline.swp` in the same directory as
`NAME`, atomically and with mode 0600. The first lines identify the file and
the time. Saving or quitting normally removes the file; a newer recovery file
found when a file is opened is offered with `recover`, and is never
overwritten until the user has recovered it, discarded it or saved.

## Consequences

- Recovery needs no central state: the file name alone finds the copy.
- The recovery file inherits the directory's location and backups, and is
  hidden on Unix systems. Untitled buffers have no file name and are not
  autosaved.
- A directory the user cannot write to gets no recovery file; autosave then
  fails quietly for that buffer.
