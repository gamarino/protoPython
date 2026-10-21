# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    23.19 |    32.86 |  1.42x |    21.33 |  0.92x |    N/A   |   N/A  |
| int_sum_loop           |   498.21 |   516.15 |  1.04x |   468.51 |  0.94x |   295.12 |  0.59x |
| list_append_loop       |    24.72 |    32.73 |  1.32x |   142.72 |  5.77x |   163.96 |  6.63x |
| str_concat_loop        |    24.62 |    34.73 |  1.41x |   124.73 |  5.07x |   188.63 |  7.66x |
| range_iterate          |   312.00 |   394.47 |  1.26x |   500.39 |  1.60x |   329.09 |  1.05x |
| multithread_cpu        |   187.04 |   637.30 |  3.41x |    96.74 |  0.52x |    52.49 |  0.28x |
| attr_lookup            |   359.50 |   569.87 |  1.59x |  1582.63 |  4.40x |   836.51 |  2.33x |
| call_recursion         |    33.99 |    43.35 |  1.28x |   187.36 |  5.51x |   157.61 |  4.64x |
| memory_pressure [INFO] |    54.42 |    59.75 |  1.10x |  1479.55 | 27.19x |  1531.44 | 28.14x |
| pyperf_fib             |    91.05 |   106.31 |  1.17x |   436.65 |  4.80x |    N/A   |   N/A  |
| pyperf_binary_trees    |    35.29 |    45.08 |  1.28x |   240.49 |  6.81x |    N/A   |   N/A  |
| pyperf_nqueens         |    49.87 |    55.11 |  1.11x |   112.89 |  2.26x |    N/A   |   N/A  |
| pyperf_richards_lite   |    24.46 |    34.77 |  1.42x |    37.34 |  1.53x |    N/A   |   N/A  |
| pyperf_sieve           |    26.41 |    33.68 |  1.28x |    95.92 |  3.63x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.39x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.57x**
* protopyc / CPython-t: **1.92x**
