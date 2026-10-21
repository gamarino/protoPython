# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    24.19 |    36.19 |  1.50x |    21.33 |  0.88x |    N/A   |   N/A  |
| int_sum_loop           |   506.56 |   518.70 |  1.02x |   469.42 |  0.93x |   308.15 |  0.61x |
| list_append_loop       |    32.10 |    39.41 |  1.23x |   165.86 |  5.17x |   180.26 |  5.62x |
| str_concat_loop        |    28.21 |    36.34 |  1.29x |   145.65 |  5.16x |   185.28 |  6.57x |
| range_iterate          |   343.06 |   422.42 |  1.23x |   554.28 |  1.62x |   377.45 |  1.10x |
| multithread_cpu        |   294.40 |  1056.41 |  3.59x |   179.67 |  0.61x |    91.02 |  0.31x |
| attr_lookup            |   374.42 |   567.31 |  1.52x |  1577.80 |  4.21x |   867.68 |  2.32x |
| call_recursion         |    35.97 |    45.97 |  1.28x |   200.41 |  5.57x |   171.31 |  4.76x |
| memory_pressure [INFO] |    50.69 |    57.49 |  1.13x |  1387.63 | 27.37x |  1457.35 | 28.75x |
| pyperf_fib             |    87.26 |   103.47 |  1.19x |   409.49 |  4.69x |    N/A   |   N/A  |
| pyperf_binary_trees    |    34.29 |    45.64 |  1.33x |   247.07 |  7.21x |    N/A   |   N/A  |
| pyperf_nqueens         |    48.74 |    55.89 |  1.15x |   103.07 |  2.11x |    N/A   |   N/A  |
| pyperf_richards_lite   |    27.66 |    35.73 |  1.29x |    37.55 |  1.36x |    N/A   |   N/A  |
| pyperf_sieve           |    28.25 |    41.25 |  1.46x |    97.18 |  3.44x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.39x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.52x**
* protopyc / CPython-t: **1.88x**
