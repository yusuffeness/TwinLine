# 0004. Exact results first, approximation on request

Status: accepted (v0.4.0, extended in v0.8.0)

## Context

Comparing every pair of lines is quadratic. Approximate methods such as
MinHash with locality-sensitive hashing are much faster, but can miss pairs,
and their output varies with the hash functions chosen.

## Decision

The default strategy, `jaccard`, is exact: it compares every pair, but skips
pairs that provably cannot matter (a size-ratio bound and an early exit from
the intersection), so its result equals a full all-pairs scan. `minhash` is
offered for large files and must be chosen explicitly. Every candidate it
finds is scored exactly, so it never reports a pair that is not similar; it
can only miss some. `bench_accuracy` measures how many, and CI fails if it
ever reports a false pair.

## Consequences

- Users get exact answers unless they ask for speed, and know what the
  trade-off is.
- The exact search stays the reference that the faster strategies are
  tested against.
- Very large files need `--minhash`; the documentation says so.
