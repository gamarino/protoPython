# protoPython with C++ exceptions — final, 2026-10-21

`perf/cpp-exceptions` at `8a367f65`, Release, protoCore 3.0.0. A Python exception
is a C++ exception (`PyException`), a Python `try` a C++ `try`; no pending
flag; the exception being handled is kept per activation. Baseline ("before"):
`../2026-10-21-pre-exceptions/` (`perf/fast-paths` at `df185d1a`, the pending
flag), measured the same way.

`run.sh`: `benchmarks/run_4way_interleaved.py`, three rounds behind the quiet
gate (1.63-1.83 busy CPUs), fresh library copy per round; medians.

| Benchmark | CPython 3.14t ms | protopy ms | protopy ratio | protopyc ms | protopyc ratio | before: protopy / protopyc |
|---|---:|---:|---:|---:|---:|---:|
| `startup_empty` | 24.2 | 21.3 | 0.85x | — | — | 0.91x / — |
| `int_sum_loop` | 512.1 | 463.8 | 0.91x | 305.4 | 0.58x | 1.03x / 0.61x |
| `list_append_loop` | 32.1 | 165.9 | 5.35x | 180.3 | 6.12x | 6.15x / 6.80x |
| `str_concat_loop` | 28.2 | 145.7 | 5.36x | 196.9 | 7.30x | 5.63x / 7.90x |
| `range_iterate` | 343.1 | 554.3 | 1.62x | 377.4 | 1.05x | 1.80x / 1.07x |
| `multithread_cpu` | 222.5 | 118.1 | **0.60x** | 67.8 | **0.31x** | 0.59x / 0.25x |
| `attr_lookup` | 373.1 | 1577.8 | 4.24x | 867.7 | 2.32x | 4.57x / 2.37x |
| `call_recursion` | 36.0 | 205.0 | 5.57x | 171.3 | 4.76x | 5.50x / 4.61x |
| `pyperf_fib` | 90.3 | 422.5 | 4.69x | — | — | 4.66x / — |
| `pyperf_binary_trees` | 36.0 | 252.7 | 7.01x | — | — | 14.21x / — |
| `pyperf_nqueens` | 53.0 | 103.1 | 1.97x | — | — | 2.32x / — |
| `pyperf_richards_lite` | 27.7 | 38.4 | 1.44x | — | — | 1.56x / — |
| `pyperf_sieve` | 29.1 | 97.2 | 3.44x | — | — | 3.48x / — |
| `memory_pressure` (informational) | 50.7 | 1564.9 | 27.4x | 1650.6 | 28.8x | 27.6x / 28.6x |
| **Geomean** (harness, `memory_pressure` excluded) | | | **2.52x** | | **1.91x** | 2.82x / 1.92x |

Exception probes (`../2026-10-21-pre-exceptions/exception_probes/`), `perf stat
-r 3`:

| Probe (cycles per iteration) | before | now | CPython 3.14t |
|---|---:|---:|---:|
| empty `while` loop | 139 | 107 | 174 |
| `try` that does not raise | 238 | 161 | 178 |
| `raise ValueError("x")` caught | 192,657 | 83,539 | 1,115 |
| raise of an existing exception | 32,446 | 28,142 | 1,714 |
| `KeyError` from `d[k]` caught | 205,103 | 95,674 | 1,073 |
| `hasattr` of a missing attribute | 11,441 | 11,639 | 682 |
| generator exhausted | 397,840 | 262,659 | 991 |
| instance of a plain class | 22,345 | 13,938 | 850 |
