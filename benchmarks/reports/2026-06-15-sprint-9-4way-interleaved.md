# protoPython 4-way interleaved benchmark (sprint-9)

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    26.74 |    36.76 |  1.37x |    25.14 |  0.94x |    N/A   |   N/A  |
| int_sum_loop           |    31.04 |    45.09 |  1.45x |   178.54 |  5.75x |   188.26 |  6.06x |
| list_append_loop       |    28.12 |    38.17 |  1.36x |   211.62 |  7.53x |   206.81 |  7.36x |
| str_concat_loop        |    29.29 |    38.59 |  1.32x |   191.57 |  6.54x |   277.52 |  9.48x |
| range_iterate          |    30.10 |    42.29 |  1.41x |   192.93 |  6.41x |   237.43 |  7.89x |
| multithread_cpu        |   282.61 |   835.90 |  2.96x |   980.25 |  3.47x |    29.38 |  0.10x |
| attr_lookup            |    42.30 |    55.78 |  1.32x |    88.27 |  2.09x |   106.49 |  2.52x |
| call_recursion         |    52.56 |    74.96 |  1.43x |   135.13 |  2.57x |    72.23 |  1.37x |
| memory_pressure [INFO] |    94.63 |    97.81 |  1.03x |  2628.50 | 27.78x |  2561.18 | 27.06x |
| pyperf_fib             |   133.20 |   141.86 |  1.07x |  1205.81 |  9.05x |   322.71 |  2.42x |
| pyperf_binary_trees    |    54.56 |    73.42 |  1.35x |    N/A   |   N/A  |   957.24 | 17.54x |
| pyperf_nqueens         |    72.56 |    75.39 |  1.04x |   388.28 |  5.35x |   470.16 |  6.48x |
| pyperf_richards_lite   |    32.13 |    43.82 |  1.36x |    N/A   |   N/A  |    40.75 |  1.27x |
| pyperf_sieve           |    36.86 |    48.14 |  1.31x |   236.04 |  6.40x |   167.26 |  4.54x |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.39x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **4.32x**
* protopyc / CPython-t: **3.34x**
