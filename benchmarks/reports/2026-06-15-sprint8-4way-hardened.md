# protoPython 4-way interleaved benchmark (sprint-9)

> **Note (2026-09-15):** "(sprint-9)" in the title is the heading hard-coded in `benchmarks/run_4way_interleaved.py`, not the measurement round. As the file name says, this report measures the sprint-8 build: commit e0d96c9b added it as sample output of the harness validation, after the sprint-9 change had been reverted in b4aec0bf.

> **Note (2026-09-15):** the protopyc `multithread_cpu` figure below, and the protopyc geomean that includes it, are invalid (the protopyc build of this period skipped `main()` in scripts that import `_thread`, so the figure measures module initialisation, not the workload; see [2026-06-15-sprint8-4way-honest.md](2026-06-15-sprint8-4way-honest.md)). The report is otherwise unchanged.

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    30.85 |    44.01 |  1.43x |    29.47 |  0.96x |    N/A   |   N/A  |
| int_sum_loop           |    33.28 |    53.92 |  1.62x |   219.02 |  6.58x |   216.53 |  6.51x |
| list_append_loop       |    34.99 |    47.41 |  1.35x |   238.06 |  6.80x |   258.03 |  7.37x |
| str_concat_loop        |    31.55 |    45.82 |  1.45x |   211.90 |  6.72x |   314.87 |  9.98x |
| range_iterate          |    39.81 |    42.21 |  1.06x |   212.93 |  5.35x |   248.17 |  6.23x |
| multithread_cpu        |   189.70 |   623.32 |  3.29x |   636.81 |  3.36x |    21.16 |  0.11x |
| attr_lookup            |    33.51 |    39.09 |  1.17x |    66.66 |  1.99x |    89.86 |  2.68x |
| call_recursion         |    38.59 |    44.70 |  1.16x |    93.65 |  2.43x |    59.42 |  1.54x |
| memory_pressure [INFO] |    59.73 |    61.10 |  1.02x |  1977.37 | 33.10x |  1805.25 | 30.22x |
| pyperf_fib             |   106.51 |   105.56 |  0.99x |   885.47 |  8.31x |    N/A   |   N/A  |
| pyperf_binary_trees    |    44.02 |    48.48 |  1.10x |  1789.38 | 40.65x |    N/A   |   N/A  |
| pyperf_nqueens         |    56.17 |    64.47 |  1.15x |   296.79 |  5.28x |    N/A   |   N/A  |
| pyperf_richards_lite   |    36.71 |    56.49 |  1.54x |    88.45 |  2.41x |    N/A   |   N/A  |
| pyperf_sieve           |    29.51 |    35.47 |  1.20x |   225.35 |  7.64x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.35x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **4.85x**
* protopyc / CPython-t: **2.81x**
