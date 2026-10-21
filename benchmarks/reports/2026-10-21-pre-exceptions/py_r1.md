# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    22.63 |    32.46 |  1.43x |    21.05 |  0.93x |    N/A   |   N/A  |
| int_sum_loop           |   509.73 |   532.03 |  1.04x |   528.40 |  1.04x |   308.57 |  0.61x |
| list_append_loop       |    25.53 |    35.88 |  1.41x |   165.33 |  6.48x |   180.18 |  7.06x |
| str_concat_loop        |    23.15 |    35.48 |  1.53x |   127.05 |  5.49x |   178.48 |  7.71x |
| range_iterate          |   334.25 |   403.73 |  1.21x |   565.17 |  1.69x |   333.97 |  1.00x |
| multithread_cpu        |   175.82 |   631.78 |  3.59x |   127.22 |  0.72x |    62.77 |  0.36x |
| attr_lookup            |   372.96 |   556.99 |  1.49x |  1653.06 |  4.43x |   841.24 |  2.26x |
| call_recursion         |    34.02 |    42.76 |  1.26x |   188.49 |  5.54x |   155.24 |  4.56x |
| memory_pressure [INFO] |    62.22 |    71.35 |  1.15x |  1718.13 | 27.62x |  1758.63 | 28.27x |
| pyperf_fib             |    95.87 |   122.29 |  1.28x |   461.54 |  4.81x |    N/A   |   N/A  |
| pyperf_binary_trees    |    34.35 |    44.58 |  1.30x |   488.59 | 14.23x |    N/A   |   N/A  |
| pyperf_nqueens         |    54.12 |    59.27 |  1.10x |   120.10 |  2.22x |    N/A   |   N/A  |
| pyperf_richards_lite   |    24.76 |    35.29 |  1.42x |    38.54 |  1.56x |    N/A   |   N/A  |
| pyperf_sieve           |    26.62 |    35.82 |  1.35x |    92.77 |  3.48x |    N/A   |   N/A  |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.42x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **2.86x**
* protopyc / CPython-t: **1.98x**
