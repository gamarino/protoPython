# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    24.18 |    35.33 |  1.46x |    23.42 |  0.97x |    N/A   |   N/A  |
| int_sum_loop           |   538.74 |   558.47 |  1.04x |   494.33 |  0.92x |   326.73 |  0.61x |
| list_append_loop       |    26.42 |    36.78 |  1.39x |   161.90 |  6.13x |   179.69 |  6.80x |
| str_concat_loop        |    26.35 |    39.29 |  1.49x |   141.91 |  5.39x |   197.63 |  7.50x |
| range_iterate          |   327.90 |   426.92 |  1.30x |   512.71 |  1.56x |   354.83 |  1.08x |
| multithread_cpu        |   212.68 |   693.66 |  3.26x |   124.20 |  0.58x |    49.55 |  0.23x |
| attr_lookup            |   385.32 |   603.03 |  1.57x |  1598.73 |  4.15x |   898.10 |  2.33x |
| call_recursion         |    36.09 |    48.23 |  1.34x |   200.41 |  5.55x |   177.47 |  4.92x |
| memory_pressure [INFO] |    56.50 |    71.43 |  1.26x |  1619.77 | 28.67x |  1652.08 | 29.24x |
| pyperf_fib             |    97.17 |   115.68 |  1.19x |   436.85 |  4.50x |    N/A   |   N/A  |
| pyperf_binary_trees    |    38.44 |    48.71 |  1.27x |   264.97 |  6.89x |    N/A   |   N/A  |
| pyperf_nqueens         |    55.36 |    65.60 |  1.19x |   108.25 |  1.96x |    N/A   |   N/A  |
| pyperf_richards_lite   |    27.26 |    39.25 |  1.44x |    41.22 |  1.51x |    N/A   |   N/A  |
| pyperf_sieve           |    28.96 |    42.11 |  1.45x |   107.35 |  3.71x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.43x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.57x**
* protopyc / CPython-t: **1.90x**
