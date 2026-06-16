# protoPython 4-way interleaved benchmark (sprint-8, honest CPython baselines)

> **Caveat on the protopyc column.**  Cross-checking after the run, the
> `protopyc` build silently skips `main()` when `_thread` is imported
> (the `__name__ == "__main__"` check evaluated under `run_module`
> returns false).  `multithread_cpu protopyc 23.75 ms` is module-init
> time, not bench work — same failure mode as the silently-crashing
> sprint-9 binary_trees that the harness misread as a 1.33× win.
> Verify any AOT row against a side-channel sanity check (e.g.,
> assertion on the computed result, not just exit code) before
> trusting it.  The two CPython columns and the `protopy` column ran
> their work end-to-end and are directly comparable.

Each bench runs the four binaries interleaved (warmup x2 each, then N=5 interleaved rounds), so any system load shift hits every column equally — directly comparable wall-clocks.

Columns:

* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**

* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**

* **protopy** — bytecode interpreter (no GIL by construction)

* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)


| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |
|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|
| startup_empty          |    24.32 |    32.65 |  1.34x |    23.36 |  0.96x |    N/A   |   N/A  |
| int_sum_loop           |    25.93 |    31.39 |  1.21x |   158.72 |  6.12x |   156.97 |  6.05x |
| list_append_loop       |    26.07 |    30.92 |  1.19x |   185.63 |  7.12x |   181.65 |  6.97x |
| str_concat_loop        |    28.94 |    35.40 |  1.22x |   168.02 |  5.81x |   258.34 |  8.93x |
| range_iterate          |    26.61 |    37.83 |  1.42x |   167.75 |  6.30x |   202.40 |  7.61x |
| multithread_cpu        |   201.22 |   603.01 |  3.00x |   659.25 |  3.28x |    23.75 |  0.12x |
| attr_lookup            |    34.22 |    37.22 |  1.09x |    62.45 |  1.82x |    79.46 |  2.32x |
| call_recursion         |    37.05 |    43.17 |  1.17x |    86.96 |  2.35x |    56.45 |  1.52x |
| memory_pressure [INFO] |    53.53 |    59.94 |  1.12x |  1835.01 | 34.28x |  1682.16 | 31.43x |
| pyperf_fib             |    96.41 |    99.04 |  1.03x |   827.11 |  8.58x |   220.93 |  2.29x |
| pyperf_binary_trees    |    44.11 |    54.37 |  1.23x |  2037.89 | 46.20x |   884.99 | 20.06x |
| pyperf_nqueens         |    51.02 |    57.06 |  1.12x |   262.72 |  5.15x |   384.43 |  7.53x |
| pyperf_richards_lite   |    29.39 |    38.52 |  1.31x |    69.63 |  2.37x |    36.10 |  1.23x |
| pyperf_sieve           |    30.29 |    39.11 |  1.29x |   208.27 |  6.88x |   131.38 |  4.34x |

## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)
* CPython (GIL on) / CPython-t: **1.30x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)
* protopy / CPython-t: **4.80x**
* protopyc / CPython-t: **3.38x**
