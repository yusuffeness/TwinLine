# 0003. Analysis on a snapshot of the document

Status: accepted (v0.2.0)

## Context

An analysis of a large file takes seconds. The editor must stay usable
meanwhile, and the analysis must not see a document that changes under it.
Locking the document for the whole analysis would freeze editing; locking
per line would make every comparison slower and the result depend on timing.

## Decision

`analyze` copies the document under the session lock and gives the copy to
the background job, which owns it until it finishes. Editing continues on
the original.

## Consequences

- The analysis takes no locks in its inner loops and its result is
  deterministic.
- The copy costs memory and time proportional to the document, which is
  small next to the analysis itself.
- A result describes the document as it was when the analysis started.
  Commands that act on results, such as `dedupe`, check that the document
  is still in the same undo state and refuse to act on stale line numbers.
