# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    25.05 |    34.65 |  1.38x |    23.60 |  0.94x |    N/A   |   N/A  |
| int_sum_loop           |   527.60 |   560.98 |  1.06x |   488.46 |  0.93x |   327.71 |  0.62x |
| list_append_loop       |    28.33 |    42.47 |  1.50x |   185.38 |  6.54x |   181.15 |  6.39x |
| str_concat_loop        |    25.07 |    40.68 |  1.62x |   143.61 |  5.73x |   202.75 |  8.09x |
| range_iterate          |   341.30 |   415.78 |  1.22x |   505.73 |  1.48x |   333.82 |  0.98x |
| multithread_cpu        |   227.93 |   656.22 |  2.88x |   104.05 |  0.46x |    47.98 |  0.21x |
| attr_lookup            |   382.26 |   581.69 |  1.52x |  1602.99 |  4.19x |   938.66 |  2.46x |
| call_recursion         |    36.85 |    47.76 |  1.30x |   208.87 |  5.67x |   170.60 |  4.63x |
| memory_pressure [INFO] |    55.04 |    64.68 |  1.18x |  1613.00 | 29.31x |  1630.27 | 29.62x |
| pyperf_fib             |    93.68 |   114.65 |  1.22x |   433.58 |  4.63x |    N/A   |   N/A  |
| pyperf_binary_trees    |    40.30 |    50.33 |  1.25x |   258.91 |  6.42x |    N/A   |   N/A  |
| pyperf_nqueens         |    55.39 |    61.22 |  1.11x |   110.27 |  1.99x |    N/A   |   N/A  |
| pyperf_richards_lite   |    27.76 |    37.72 |  1.36x |    40.96 |  1.48x |    N/A   |   N/A  |
| pyperf_sieve           |    28.79 |    39.36 |  1.37x |   101.71 |  3.53x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.40x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.52x**
* protopyc / CPython-t: **1.85x**
