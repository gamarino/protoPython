# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    22.87 |    31.13 |  1.36x |    20.03 |  0.88x |    N/A   |   N/A  |
| int_sum_loop           |   501.11 |   541.25 |  1.08x |   467.32 |  0.93x |   301.24 |  0.60x |
| list_append_loop       |    24.56 |    32.10 |  1.31x |   146.63 |  5.97x |   159.22 |  6.48x |
| str_concat_loop        |    23.68 |    33.43 |  1.41x |   129.61 |  5.47x |   181.74 |  7.68x |
| range_iterate          |   368.21 |   465.93 |  1.27x |   543.26 |  1.48x |   349.23 |  0.95x |
| multithread_cpu        |   195.87 |   648.80 |  3.31x |    99.74 |  0.51x |    45.96 |  0.23x |
| attr_lookup            |   366.13 |   573.55 |  1.57x |  1642.51 |  4.49x |   828.98 |  2.26x |
| call_recursion         |    37.51 |    48.75 |  1.30x |   200.53 |  5.35x |   173.97 |  4.64x |
| memory_pressure [INFO] |    60.84 |    70.53 |  1.16x |  1739.50 | 28.59x |  1766.04 | 29.03x |
| pyperf_fib             |    99.73 |   114.60 |  1.15x |   463.88 |  4.65x |    N/A   |   N/A  |
| pyperf_binary_trees    |    35.39 |    46.97 |  1.33x |   255.51 |  7.22x |    N/A   |   N/A  |
| pyperf_nqueens         |    52.45 |    59.61 |  1.14x |   112.07 |  2.14x |    N/A   |   N/A  |
| pyperf_richards_lite   |    25.38 |    34.43 |  1.36x |    36.01 |  1.42x |    N/A   |   N/A  |
| pyperf_sieve           |    28.80 |    36.47 |  1.27x |    94.68 |  3.29x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.38x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.51x**
* protopyc / CPython-t: **1.83x**
