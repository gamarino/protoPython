# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    22.51 |    32.76 |  1.46x |    20.27 |  0.90x |    N/A   |   N/A  |
| int_sum_loop           |   488.45 |   508.77 |  1.04x |   502.55 |  1.03x |   300.83 |  0.62x |
| list_append_loop       |    23.66 |    32.64 |  1.38x |   145.49 |  6.15x |   158.41 |  6.70x |
| str_concat_loop        |    22.32 |    32.89 |  1.47x |   125.67 |  5.63x |   179.70 |  8.05x |
| range_iterate          |   300.89 |   393.50 |  1.31x |   540.74 |  1.80x |   325.61 |  1.08x |
| multithread_cpu        |   197.70 |   610.47 |  3.09x |   115.52 |  0.58x |    48.81 |  0.25x |
| attr_lookup            |   358.38 |   543.15 |  1.52x |  1671.63 |  4.66x |   850.36 |  2.37x |
| call_recursion         |    33.42 |    43.64 |  1.31x |   183.94 |  5.50x |   154.23 |  4.61x |
| memory_pressure [INFO] |    53.51 |    59.15 |  1.11x |  1467.70 | 27.43x |  1530.41 | 28.60x |
| pyperf_fib             |    87.74 |   108.15 |  1.23x |   408.65 |  4.66x |    N/A   |   N/A  |
| pyperf_binary_trees    |    33.81 |    42.10 |  1.25x |   480.36 | 14.21x |    N/A   |   N/A  |
| pyperf_nqueens         |    49.93 |    58.31 |  1.17x |   115.92 |  2.32x |    N/A   |   N/A  |
| pyperf_richards_lite   |    25.67 |    37.16 |  1.45x |    39.83 |  1.55x |    N/A   |   N/A  |
| pyperf_sieve           |    36.45 |    48.03 |  1.32x |   115.65 |  3.17x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.41x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.80x**
* protopyc / CPython-t: **1.92x**
