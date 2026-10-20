# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    23.92 |    42.03 |  1.76x |    23.73 |  0.99x |    N/A   |   N/A  |
| int_sum_loop           |   468.84 |   500.41 |  1.07x |   500.88 |  1.07x |   291.99 |  0.62x |
| list_append_loop       |    23.33 |    31.94 |  1.37x |   143.30 |  6.14x |   159.19 |  6.82x |
| str_concat_loop        |    22.74 |    31.73 |  1.39x |   132.98 |  5.85x |   200.31 |  8.81x |
| range_iterate          |   313.26 |   391.75 |  1.25x |   579.58 |  1.85x |   335.37 |  1.07x |
| multithread_cpu        |   174.26 |   631.08 |  3.62x |   113.62 |  0.65x |    43.37 |  0.25x |
| attr_lookup            |   346.81 |   539.86 |  1.56x |  2104.96 |  6.07x |  1115.51 |  3.22x |
| call_recursion         |    32.66 |    40.83 |  1.25x |   184.30 |  5.64x |   155.79 |  4.77x |
| memory_pressure [INFO] |    50.97 |    56.79 |  1.11x |  1474.47 | 28.93x |  1603.19 | 31.45x |
| pyperf_fib             |    86.72 |    98.83 |  1.14x |   396.72 |  4.57x |    N/A   |   N/A  |
| pyperf_binary_trees    |    33.38 |    42.60 |  1.28x |   492.37 | 14.75x |    N/A   |   N/A  |
| pyperf_nqueens         |    49.57 |    54.66 |  1.10x |   119.70 |  2.41x |    N/A   |   N/A  |
| pyperf_richards_lite   |    23.78 |    33.01 |  1.39x |    41.08 |  1.73x |    N/A   |   N/A  |
| pyperf_sieve           |    25.25 |    32.81 |  1.30x |    91.13 |  3.61x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.42x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **3.00x**
* protopyc / CPython-t: **2.05x**
