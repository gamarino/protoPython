# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    22.58 |    32.73 |  1.45x |    20.48 |  0.91x |    N/A   |   N/A  |
| int_sum_loop           |   496.14 |   519.33 |  1.05x |   510.05 |  1.03x |   304.74 |  0.61x |
| list_append_loop       |    23.60 |    32.57 |  1.38x |   143.30 |  6.07x |   160.56 |  6.80x |
| str_concat_loop        |    22.86 |    33.07 |  1.45x |   129.18 |  5.65x |   180.67 |  7.90x |
| range_iterate          |   303.98 |   399.58 |  1.31x |   556.93 |  1.83x |   325.17 |  1.07x |
| multithread_cpu        |   193.07 |   643.52 |  3.33x |   113.91 |  0.59x |    48.43 |  0.25x |
| attr_lookup            |   360.38 |   546.15 |  1.52x |  1646.50 |  4.57x |   867.11 |  2.41x |
| call_recursion         |    33.43 |    43.91 |  1.31x |   182.46 |  5.46x |   155.38 |  4.65x |
| memory_pressure [INFO] |    53.81 |    59.04 |  1.10x |  1531.64 | 28.46x |  1537.43 | 28.57x |
| pyperf_fib             |    90.41 |   107.29 |  1.19x |   413.89 |  4.58x |    N/A   |   N/A  |
| pyperf_binary_trees    |    35.12 |    52.75 |  1.50x |   482.13 | 13.73x |    N/A   |   N/A  |
| pyperf_nqueens         |    52.84 |    61.05 |  1.16x |   123.88 |  2.34x |    N/A   |   N/A  |
| pyperf_richards_lite   |    24.71 |    34.48 |  1.40x |    38.62 |  1.56x |    N/A   |   N/A  |
| pyperf_sieve           |    26.77 |    35.17 |  1.31x |    94.49 |  3.53x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.42x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.82x**
* protopyc / CPython-t: **1.93x**
