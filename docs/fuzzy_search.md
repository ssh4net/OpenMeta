# Fuzzy Search

Fuzzy Search is an optional inspection layer over decoded metadata entry names
and property paths. It is separate from Semantic Query: fuzzy ranking helps a
person find likely fields, but it never changes metadata interpretation.

Enable it with:

```bash
cmake -S . -B build \
  -DOPENMETA_ENABLE_RAPIDFUZZ=ON \
  -DCMAKE_PREFIX_PATH=/path/to/rapidfuzz/prefix
```

The public entry point is
`fuzzy_search_metadata(...)` in
`openmeta/metadata_fuzzy_search.h`. Python exposes the same bounded operation
through `Document.fuzzy_search(...)` and
`TransferSourceSnapshot.fuzzy_search(...)`.

## Ranking Contract

Results use deterministic ordering:

1. Higher score.
2. Exact, alias, then general fuzzy provenance.
3. Lower stable entry ID.

Exact normalized phrases score `100`. Exact curated aliases score `96`.
Misspelled curated aliases use whole-phrase and per-token edit similarity and
score at most `95`; this prevents a shared word such as `sensor` or `rectangle`
from making an unrelated phrase look like a known alias. General candidate
matching uses RapidFuzz weighted and token similarity.

The default minimum score is `80`. Hosts should expose the score and provenance
instead of presenting a fuzzy result as an exact metadata meaning.

## Text Contract

The current contract is locale-independent ASCII:

- ASCII case is folded.
- Punctuation and separators collapse to spaces.
- Camel-case and acronym-to-word boundaries are split, so `GPSLatitude` becomes
  `gps latitude`.
- Non-ASCII query bytes return `UnsupportedQueryText`.
- Non-ASCII bytes in candidate names act as separators. Searchable ASCII
  fragments remain visible, but OpenMeta does not claim Unicode equivalence or
  transliteration.

Unicode normalization, transliteration, and multilingual ranking remain a
future milestone. They must not be added as implicit locale-dependent behavior.

## Resource And Threading Contract

Query bytes, candidate bytes, and result counts have public hard limits.
Deleted entries are skipped and returned name/group strings carry truncation
flags. Each call uses only local search state and reads a `const MetaStore`, so
concurrent searches are safe when the finalized store is not being mutated.

The current implementation performs a bounded linear scan. It does not retain
references to the store or maintain global caches.

## Quality And Performance Gates

The RapidFuzz release gate covers curated spelling errors and aliases across
descriptive, capture, geometry, color, RAW-processing, rights, location, and
history terminology. It also includes unrelated and metadata-like adversarial
negative queries, deterministic top-k behavior, deleted entries, resource
bounds, and the ASCII/UTF-8 contract.

Build the informational benchmark with:

```bash
cmake -S . -B build-fuzzy-benchmark \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENMETA_ENABLE_RAPIDFUZZ=ON \
  -DOPENMETA_BUILD_BENCHMARKS=ON
cmake --build build-fuzzy-benchmark \
  --target openmeta_benchmark_fuzzy_search
./build-fuzzy-benchmark/openmeta_benchmark_fuzzy_search
```

The benchmark validates every result and reports elapsed time, queries per
second, and nanoseconds per entry/query. It has no timing pass/fail threshold.

A reference Release/libc++ run measured:

| Entries | Approximate time per query |
| ---: | ---: |
| 256 | 2.3 ms |
| 1,024 | 8.2 ms |
| 4,096 | 31.7 ms |
| 16,384 | 129 ms |

These values are diagnostic, not a performance guarantee. The near-constant
per-entry cost confirms linear scaling.

## Indexing Decision

The linear scan remains the default for this milestone. It is simple, bounded,
thread-safe for immutable stores, and adequate for ordinary per-image metadata
stores and one-off searches.

An immutable reusable index is justified for repeated interactive queries or
aggregated stores with many thousands of entries. That work is deferred until
Fuzzy Search resumes, before Adapters and Utilities become the active project
focus. The index must have explicit ownership, build cost, invalidation rules,
memory accounting, and identical deterministic ranking.

Fuzzy Search is considered milestone-ready at about `80-85%`. Remaining work is
broader independently sourced quality coverage, Unicode/transliteration policy,
multilingual ranking gates, and the optional immutable index.
