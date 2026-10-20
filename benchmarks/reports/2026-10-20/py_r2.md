# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    21.63 |    30.55 |  1.41x |    21.03 |  0.97x |    N/A   |   N/A  |
| int_sum_loop           |   478.74 |   504.61 |  1.05x |   492.77 |  1.03x |   293.71 |  0.61x |
| list_append_loop       |    23.24 |    32.39 |  1.39x |   141.48 |  6.09x |   160.50 |  6.91x |
| str_concat_loop        |    22.08 |    30.41 |  1.38x |   131.19 |  5.94x |   199.02 |  9.01x |
| range_iterate          |   284.46 |   363.03 |  1.28x |   521.76 |  1.83x |   306.70 |  1.08x |
| multithread_cpu        |   171.35 |   597.25 |  3.49x |   109.48 |  0.64x |    43.33 |  0.25x |
| attr_lookup            |   336.42 |   511.48 |  1.52x |  2054.60 |  6.11x |  1088.14 |  3.23x |
| call_recursion         |    32.88 |    41.88 |  1.27x |   186.12 |  5.66x |   149.66 |  4.55x |
| memory_pressure [INFO] |    50.04 |    57.09 |  1.14x |  1451.94 | 29.01x |  1578.49 | 31.54x |
| pyperf_fib             |    82.69 |    98.37 |  1.19x |   395.16 |  4.78x |    N/A   |   N/A  |
| pyperf_binary_trees    |    33.24 |    42.90 |  1.29x |   486.48 | 14.64x |    N/A   |   N/A  |
| pyperf_nqueens         |    50.96 |    57.67 |  1.13x |   120.00 |  2.35x |    N/A   |   N/A  |
| pyperf_richards_lite   |    24.75 |    32.86 |  1.33x |    40.07 |  1.62x |    N/A   |   N/A  |
| pyperf_sieve           |    25.60 |    33.70 |  1.32x |    93.09 |  3.64x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.39x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.97x**
* protopyc / CPython-t: **2.05x**
