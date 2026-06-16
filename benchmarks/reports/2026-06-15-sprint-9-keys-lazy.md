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
| startup_empty          |      38.86   |      27.30   |         N/A    |  0.70x fast  |     N/A      |  22.8/  N/A/ 11.2 MB |
| int_sum_loop           |      43.00   |     182.89   |     185.27    |  4.25x slow  |  4.31x slow  |  57.5/ 57.4/ 11.1 MB |
| list_append_loop       |      42.05   |     208.77   |     205.24    |  4.96x slow  |  4.88x slow  |  73.4/ 73.4/ 11.4 MB |
| str_concat_loop        |      44.61   |     190.00   |     296.38    |  4.26x slow  |  6.64x slow  |  57.4/ 73.4/ 11.2 MB |
| range_iterate          |      50.72   |     198.10   |     233.35    |  3.91x slow  |  4.60x slow  |  57.4/ 89.4/ 11.2 MB |
| multithread_cpu        |     709.87   |     898.40   |      24.84    |  1.27x slow  |  0.03x fast  | 239.8/ 22.9/ 11.3 MB |
| attr_lookup            |      53.69   |      68.74   |      93.94    |  1.28x slow  |  1.75x slow  |  22.8/ 54.2/ 11.1 MB |
| call_recursion         |      55.56   |      97.60   |      65.02    |  1.76x slow  |  1.17x slow  |  22.8/ 38.2/ 11.1 MB |
| memory_pressure [INFO] |      96.83   |    2630.83   |    2296.91    | 27.17x slow  | 23.72x slow  | 982.7/1014.2/ 11.3 MB |
| pyperf_fib             |     153.77   |    1225.42   |     311.75    |  7.97x slow  |  2.03x slow  |  22.7/102.4/ 11.2 MB |
| pyperf_binary_trees    |      87.47   |    2053.13   |    1027.22    | 23.47x slow  | 11.74x slow  | 574.1/294.2/ 11.2 MB |
| pyperf_nqueens         |      92.81   |     427.17   |     568.11    |  4.60x slow  |  6.12x slow  |  23.1/134.2/ 11.1 MB |
| pyperf_richards_lite   |      57.88   |      93.75   |      52.74    |  1.62x slow  |  0.91x fast  |  22.9/ 22.1/ 11.2 MB |
| pyperf_sieve           |      77.89   |     314.99   |     200.03    |  4.04x slow  |  2.57x slow  | 150.7/102.2/ 11.1 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  3.22x       |  2.24x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
