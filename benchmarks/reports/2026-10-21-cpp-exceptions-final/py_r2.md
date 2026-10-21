# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    23.65 |    32.09 |  1.36x |    20.08 |  0.85x |    N/A   |   N/A  |
| int_sum_loop           |   530.10 |   519.06 |  0.98x |   455.39 |  0.86x |   305.44 |  0.58x |
| list_append_loop       |    35.53 |    48.18 |  1.36x |   225.82 |  6.36x |   222.30 |  6.26x |
| str_concat_loop        |    28.94 |    43.13 |  1.49x |   162.62 |  5.62x |   211.26 |  7.30x |
| range_iterate          |   428.11 |   558.83 |  1.31x |   694.90 |  1.62x |   440.33 |  1.03x |
| multithread_cpu        |   222.53 |   750.03 |  3.37x |   118.13 |  0.53x |    67.79 |  0.30x |
| attr_lookup            |   373.11 |   566.47 |  1.52x |  1599.95 |  4.29x |   869.22 |  2.33x |
| call_recursion         |    35.36 |    46.47 |  1.31x |   205.04 |  5.80x |   170.31 |  4.82x |
| memory_pressure [INFO] |    58.49 |    66.18 |  1.13x |  1564.93 | 26.76x |  1672.78 | 28.60x |
| pyperf_fib             |    90.66 |   112.40 |  1.24x |   428.67 |  4.73x |    N/A   |   N/A  |
| pyperf_binary_trees    |    39.46 |    62.22 |  1.58x |   258.83 |  6.56x |    N/A   |   N/A  |
| pyperf_nqueens         |    52.95 |    71.90 |  1.36x |   104.57 |  1.97x |    N/A   |   N/A  |
| pyperf_richards_lite   |    31.10 |    45.47 |  1.46x |    44.87 |  1.44x |    N/A   |   N/A  |
| pyperf_sieve           |    32.21 |    43.71 |  1.36x |   111.63 |  3.47x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.45x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.53x**
* protopyc / CPython-t: **1.91x**
