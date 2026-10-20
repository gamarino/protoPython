# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    21.74 |    30.28 |  1.39x |    21.00 |  0.97x |    N/A   |   N/A  |
| int_sum_loop           |   481.83 |   484.27 |  1.01x |   495.74 |  1.03x |   286.54 |  0.59x |
| list_append_loop       |    23.19 |    30.98 |  1.34x |   142.84 |  6.16x |   160.26 |  6.91x |
| str_concat_loop        |    22.69 |    31.24 |  1.38x |   126.92 |  5.59x |   201.84 |  8.89x |
| range_iterate          |   284.58 |   368.46 |  1.29x |   521.51 |  1.83x |   319.99 |  1.12x |
| multithread_cpu        |   162.57 |   609.19 |  3.75x |   107.25 |  0.66x |    47.88 |  0.29x |
| attr_lookup            |   335.22 |   526.47 |  1.57x |  2043.35 |  6.10x |  1079.83 |  3.22x |
| call_recursion         |    32.27 |    41.31 |  1.28x |   181.55 |  5.63x |   153.49 |  4.76x |
| memory_pressure [INFO] |    51.04 |    56.35 |  1.10x |  1445.34 | 28.32x |  1589.07 | 31.13x |
| pyperf_fib             |    83.37 |    99.28 |  1.19x |   382.10 |  4.58x |    N/A   |   N/A  |
| pyperf_binary_trees    |    33.34 |    42.05 |  1.26x |   478.95 | 14.36x |    N/A   |   N/A  |
| pyperf_nqueens         |    47.72 |    54.58 |  1.14x |   117.80 |  2.47x |    N/A   |   N/A  |
| pyperf_richards_lite   |    24.97 |    33.49 |  1.34x |    40.46 |  1.62x |    N/A   |   N/A  |
| pyperf_sieve           |    25.17 |    36.32 |  1.44x |    93.44 |  3.71x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.41x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.97x**
* protopyc / CPython-t: **2.11x**
