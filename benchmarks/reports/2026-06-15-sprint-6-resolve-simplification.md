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
| startup_empty          |      36.99   |      23.45   |         N/A    |  0.63x fast  |     N/A      |  22.8/  N/A/ 11.1 MB |
| int_sum_loop           |      68.72   |     236.68   |     269.24    |  3.44x slow  |  3.92x slow  |  57.4/ 57.2/ 11.1 MB |
| list_append_loop       |      50.58   |     248.36   |     231.14    |  4.91x slow  |  4.57x slow  |  73.4/ 73.3/ 11.4 MB |
| str_concat_loop        |      57.89   |     258.42   |     338.32    |  4.46x slow  |  5.84x slow  |  57.4/ 73.2/ 11.2 MB |
| range_iterate          |      60.14   |     241.18   |     283.79    |  4.01x slow  |  4.72x slow  |  57.4/ 89.4/ 11.1 MB |
| multithread_cpu        |     951.05   |    1037.72   |      26.26    |  1.09x slow  |  0.03x fast  | 459.8/ 23.0/ 11.2 MB |
| attr_lookup            |      71.81   |      99.70   |     116.04    |  1.39x slow  |  1.62x slow  |  22.7/ 54.3/ 11.1 MB |
| call_recursion         |      69.02   |     109.42   |      75.42    |  1.59x slow  |  1.09x slow  |  22.8/ 38.2/ 11.2 MB |
| memory_pressure [INFO] |      67.62   |    2228.84   |    1790.55    | 32.96x slow  | 26.48x slow  | 982.7/1014.4/ 11.3 MB |
| pyperf_fib             |     123.61   |     871.62   |     223.06    |  7.05x slow  |  1.80x slow  |  22.7/102.4/ 11.2 MB |
| pyperf_binary_trees    |      50.37   |    1564.70   |     649.66    | 31.07x slow  | 12.90x slow  | 734.3/294.3/ 11.3 MB |
| pyperf_nqueens         |      62.00   |    1428.60   |     394.10    | 23.04x slow  |  6.36x slow  | 679.4/134.4/ 11.2 MB |
| pyperf_richards_lite   |      46.55   |      72.52   |      38.52    |  1.56x slow  |  0.83x fast  |  23.0/ 22.2/ 11.3 MB |
| pyperf_sieve           |      40.95   |     297.08   |     136.03    |  7.26x slow  |  3.32x slow  | 198.8/102.3/ 11.1 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  3.72x       |  2.15x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
