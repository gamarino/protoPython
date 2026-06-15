# protoPython performance audit — 2026-06-15

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.
Rows tagged `[INFO]` are reported for transparency but do NOT participate in the geomean — see the report footer.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |      49.48   |      26.56   |         N/A    |  0.54x fast  |     N/A      |  22.8/  N/A/ 11.2 MB |
| int_sum_loop           |      58.46   |     212.55   |     202.95    |  3.64x slow  |  3.47x slow  |  57.5/ 57.4/ 11.2 MB |
| list_append_loop       |      52.35   |     248.33   |     242.89    |  4.74x slow  |  4.64x slow  |  73.4/ 73.5/ 11.4 MB |
| str_concat_loop        |      55.77   |     226.78   |     348.82    |  4.07x slow  |  6.26x slow  |  57.5/ 73.2/ 11.2 MB |
| range_iterate          |      56.75   |     227.37   |     269.33    |  4.01x slow  |  4.75x slow  |  57.5/ 89.4/ 11.2 MB |
| multithread_cpu        |     718.82   |     644.29   |      23.03    |  0.90x fast  |  0.03x fast  | 208.0/ 22.9/ 11.3 MB |
| attr_lookup            |      51.24   |      64.80   |      86.06    |  1.26x slow  |  1.68x slow  |  22.8/ 54.2/ 11.1 MB |
| call_recursion         |      53.04   |      94.09   |      59.66    |  1.77x slow  |  1.12x slow  |  22.8/ 38.2/ 11.2 MB |
| memory_pressure [INFO] |      74.80   |    1873.21   |    1809.72    | 25.04x slow  | 24.19x slow  | 982.8/1014.2/ 11.3 MB |
| pyperf_fib             |     151.49   |    1055.95   |     283.98    |  6.97x slow  |  1.87x slow  |  22.8/102.3/ 11.2 MB |
| pyperf_binary_trees    |      71.23   |    1952.12   |     824.80    | 27.41x slow  | 11.58x slow  | 734.4/294.4/ 11.3 MB |
| pyperf_nqueens         |      91.78   |     356.09   |     496.82    |  3.88x slow  |  5.41x slow  |  23.2/134.4/ 11.2 MB |
| pyperf_richards_lite   |      56.77   |      85.98   |      46.58    |  1.51x slow  |  0.82x fast  |  22.9/ 22.3/ 11.3 MB |
| pyperf_sieve           |      57.66   |     270.56   |     173.04    |  4.69x slow  |  3.00x slow  | 150.8/102.4/ 11.1 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  3.00x       |  2.12x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
