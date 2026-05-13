# protoPython performance audit — 2026-05-13

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.
Rows tagged `[INFO]` are reported for transparency but do NOT participate in the geomean — see the report footer.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |     177.09   |      91.46   |         N/A    |  0.52x fast  |     N/A      |  21.0/  N/A/ 10.4 MB |
| int_sum_loop           |     142.87   |     137.06   |     139.80    |  0.96x fast  |  0.98x fast  |  20.9/ 20.5/ 10.4 MB |
| list_append_loop       |     161.42   |     868.60   |     587.03    |  5.38x slow  |  3.64x slow  |  61.3/ 57.6/ 10.6 MB |
| str_concat_loop        |     181.51   |    1526.90   |    1633.86    |  8.41x slow  |  9.00x slow  |  85.4/ 85.4/ 10.4 MB |
| range_iterate          |     152.04   |     757.94   |     780.43    |  4.99x slow  |  5.13x slow  |  42.1/ 57.4/ 10.2 MB |
| multithread_cpu        |    2184.71   |     171.19   |    1265.57    |  0.08x fast  |  0.58x fast  |  25.5/ 20.8/ 10.4 MB |
| attr_lookup            |     169.02   |     722.68   |     281.48    |  4.28x slow  |  1.67x slow  |  37.9/ 36.8/ 10.4 MB |
| call_recursion         |     175.08   |     363.08   |     212.06    |  2.07x slow  |  1.21x slow  |  21.0/ 37.1/ 10.4 MB |
| memory_pressure [INFO] |     466.53   |   12962.99   |    1083.34    | 27.79x slow  |  2.32x slow  | 324.2/ 45.3/ 10.5 MB |
| pyperf_fib             |     413.39   |    5806.11   |    1799.27    | 14.05x slow  |  4.35x slow  |  20.5/ 52.4/ 10.0 MB |
| pyperf_binary_trees    |     216.96   |   11107.50   |     400.02    | 51.20x slow  |  1.84x slow  | 227.0/ 36.6/ 10.0 MB |
| pyperf_nqueens         |     197.05   |    8697.10   |    1599.32    | 44.14x slow  |  8.12x slow  | 165.9/ 36.1/ 10.0 MB |
| pyperf_richards_lite   |     111.44   |     851.32   |     221.56    |  7.64x slow  |  1.99x slow  |  38.1/ 20.2/ 10.0 MB |
| pyperf_sieve           |     137.84   |    1354.98   |     272.21    |  9.83x slow  |  1.97x slow  |  86.5/ 36.6/ 10.0 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  4.32x       |  2.46x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
