# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    27.32 |    32.75 |  1.20x |    22.38 |  0.82x |    N/A   |   N/A  |
| int_sum_loop           |   512.15 |   517.31 |  1.01x |   463.80 |  0.91x |   292.75 |  0.57x |
| list_append_loop       |    27.89 |    43.11 |  1.55x |   149.08 |  5.35x |   170.79 |  6.12x |
| str_concat_loop        |    25.44 |    35.68 |  1.40x |   136.49 |  5.36x |   196.93 |  7.74x |
| range_iterate          |   319.88 |   406.12 |  1.27x |   504.50 |  1.58x |   335.46 |  1.05x |
| multithread_cpu        |   186.03 |   684.41 |  3.68x |   111.84 |  0.60x |    64.71 |  0.35x |
| attr_lookup            |   367.68 |   563.72 |  1.53x |  1560.25 |  4.24x |   846.22 |  2.30x |
| call_recursion         |    39.42 |    51.06 |  1.30x |   209.92 |  5.33x |   172.89 |  4.39x |
| memory_pressure [INFO] |    49.91 |    76.12 |  1.53x |  1571.49 | 31.49x |  1650.59 | 33.07x |
| pyperf_fib             |    90.30 |   106.21 |  1.18x |   422.46 |  4.68x |    N/A   |   N/A  |
| pyperf_binary_trees    |    36.03 |    64.85 |  1.80x |   252.69 |  7.01x |    N/A   |   N/A  |
| pyperf_nqueens         |    53.16 |    66.91 |  1.26x |   102.98 |  1.94x |    N/A   |   N/A  |
| pyperf_richards_lite   |    26.26 |    43.66 |  1.66x |    38.44 |  1.46x |    N/A   |   N/A  |
| pyperf_sieve           |    29.10 |    44.61 |  1.53x |    95.58 |  3.28x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.48x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.49x**
* protopyc / CPython-t: **1.93x**
