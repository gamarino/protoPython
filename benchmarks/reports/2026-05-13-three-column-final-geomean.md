# protoPython performance audit — 2026-05-13

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |     179.84   |     117.08   |         N/A    |  0.65x fast  |     N/A      |  20.6/  N/A/ 10.1 MB |
| int_sum_loop           |     189.68   |     122.82   |      91.59    |  0.65x fast  |  0.48x fast  |  20.5/ 20.2/ 10.2 MB |
| list_append_loop       |     169.89   |    1411.66   |     816.82    |  8.31x slow  |  4.81x slow  |  61.0/ 57.3/ 10.5 MB |
| str_concat_loop        |     170.87   |    1720.01   |    1417.76    | 10.07x slow  |  8.30x slow  |  85.1/ 85.0/ 10.2 MB |
| range_iterate          |     206.74   |     637.83   |    1092.63    |  3.09x slow  |  5.29x slow  |  41.9/ 57.3/ 10.2 MB |
| multithread_cpu        |    2569.78   |     174.82   |    1288.45    |  0.07x fast  |  0.50x fast  |  25.4/ 20.6/ 10.4 MB |
| attr_lookup            |     235.32   |     777.06   |     291.03    |  3.30x slow  |  1.24x slow  |  37.7/ 36.6/ 10.2 MB |
| call_recursion         |     405.16   |     609.60   |     228.72    |  1.50x slow  |  0.56x fast  |  20.8/ 37.0/ 10.2 MB |
| memory_pressure        |     409.00   |   13765.08   |     488.66    | 33.66x slow  |  1.19x slow  | 340.0/ 37.0/ 10.4 MB |
| pyperf_fib             |     625.28   |    5899.11   |    2047.76    |  9.43x slow  |  3.27x slow  |  20.9/ 52.7/ 10.2 MB |
| pyperf_binary_trees    |     317.75   |    8452.18   |     283.91    | 26.60x slow  |  0.89x fast  | 219.5/ 36.9/ 10.2 MB |
| pyperf_nqueens         |     267.56   |    9097.66   |    1639.87    | 34.00x slow  |  6.13x slow  | 166.3/ 36.4/ 10.2 MB |
| pyperf_richards_lite   |     156.27   |     865.47   |     126.27    |  5.54x slow  |  0.81x fast  |  38.4/ 20.5/ 10.4 MB |
| pyperf_sieve           |     114.66   |    1242.89   |     220.58    | 10.84x slow  |  1.92x slow  |  86.8/ 36.8/ 10.2 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=14)         |              |              |               |  4.25x       |  1.72x       |                     |
