# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    23.02 |    32.22 |  1.40x |    20.00 |  0.87x |    N/A   |   N/A  |
| int_sum_loop           |   498.20 |   504.54 |  1.01x |   467.77 |  0.94x |   293.52 |  0.59x |
| list_append_loop       |    25.13 |    32.79 |  1.30x |   137.45 |  5.47x |   156.64 |  6.23x |
| str_concat_loop        |    22.63 |    31.70 |  1.40x |   127.23 |  5.62x |   176.73 |  7.81x |
| range_iterate          |   314.81 |   389.27 |  1.24x |   495.80 |  1.57x |   317.51 |  1.01x |
| multithread_cpu        |   176.66 |   632.82 |  3.58x |    93.59 |  0.53x |    61.21 |  0.35x |
| attr_lookup            |   365.43 |   555.52 |  1.52x |  1588.68 |  4.35x |   832.19 |  2.28x |
| call_recursion         |    33.44 |    42.44 |  1.27x |   192.43 |  5.76x |   149.33 |  4.47x |
| memory_pressure [INFO] |    52.28 |    61.16 |  1.17x |  1474.08 | 28.20x |  1528.04 | 29.23x |
| pyperf_fib             |    96.52 |   111.39 |  1.15x |   445.35 |  4.61x |    N/A   |   N/A  |
| pyperf_binary_trees    |    37.36 |    45.42 |  1.22x |   249.95 |  6.69x |    N/A   |   N/A  |
| pyperf_nqueens         |    52.18 |    55.94 |  1.07x |   112.59 |  2.16x |    N/A   |   N/A  |
| pyperf_richards_lite   |    24.50 |    35.73 |  1.46x |    36.58 |  1.49x |    N/A   |   N/A  |
| pyperf_sieve           |    27.90 |    35.82 |  1.28x |    90.95 |  3.26x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.37x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.53x**
* protopyc / CPython-t: **1.94x**
