# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    24.07 |    29.43 |  1.22x |    23.20 |  0.96x |    N/A   |   N/A  |
| int_sum_loop           |    25.29 |    33.58 |  1.33x |   164.71 |  6.51x |   156.71 |  6.20x |
| list_append_loop       |    30.03 |    33.50 |  1.12x |   193.08 |  6.43x |   188.95 |  6.29x |
| str_concat_loop        |    25.30 |    31.33 |  1.24x |   180.95 |  7.15x |   262.90 | 10.39x |
| range_iterate          |    26.09 |    37.52 |  1.44x |   183.58 |  7.04x |   217.91 |  8.35x |
| multithread_cpu        |   199.36 |   606.01 |  3.04x |   620.19 |  3.11x |    23.77 |  0.12x |
| attr_lookup            |    33.59 |    38.83 |  1.16x |    66.88 |  1.99x |    84.89 |  2.53x |
| call_recursion         |    38.26 |    41.42 |  1.08x |    89.42 |  2.34x |    56.02 |  1.46x |
| memory_pressure [INFO] |    57.31 |    61.20 |  1.07x |  1912.77 | 33.37x |  1721.69 | 30.04x |
| pyperf_fib             |   106.72 |   107.98 |  1.01x |   973.79 |  9.12x |    N/A   |   N/A  |
| pyperf_binary_trees    |    40.80 |    45.04 |  1.10x |  1420.19 | 34.81x |    N/A   |   N/A  |
| pyperf_nqueens         |    56.60 |    60.61 |  1.07x |   318.08 |  5.62x |    N/A   |   N/A  |
| pyperf_richards_lite   |    30.19 |    33.35 |  1.10x |    71.85 |  2.38x |    N/A   |   N/A  |
| pyperf_sieve           |    32.23 |    37.56 |  1.17x |   225.14 |  6.99x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.25x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **4.87x**
* protopyc / CPython-t: **2.84x**
