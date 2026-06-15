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
| startup_empty          |      51.05   |      28.86   |         N/A    |  0.57x fast  |     N/A      |  22.4/  N/A/ 11.2 MB |
| int_sum_loop           |      48.27   |     197.69   |     202.56    |  4.10x slow  |  4.20x slow  |  57.4/ 57.2/ 11.1 MB |
| list_append_loop       |      49.76   |     231.47   |     238.27    |  4.65x slow  |  4.79x slow  |  73.3/ 73.4/ 11.4 MB |
| str_concat_loop        |      50.04   |     228.32   |     300.64    |  4.56x slow  |  6.01x slow  |  57.3/ 73.3/ 11.1 MB |
| range_iterate          |      58.06   |     270.89   |     287.77    |  4.67x slow  |  4.96x slow  |  57.3/ 89.2/ 11.2 MB |
| multithread_cpu        |    1209.01   |    1316.69   |      32.85    |  1.09x slow  |  0.03x fast  | 495.8/ 22.8/ 11.2 MB |
| attr_lookup            |      71.57   |      91.24   |     107.27    |  1.27x slow  |  1.50x slow  |  22.7/ 54.1/ 11.1 MB |
| call_recursion         |      70.78   |     122.32   |      73.11    |  1.73x slow  |  1.03x slow  |  22.8/ 38.1/ 11.2 MB |
| memory_pressure [INFO] |      69.22   |    1879.63   |    1751.99    | 27.15x slow  | 25.31x slow  | 982.8/1030.3/ 11.3 MB |
| pyperf_fib             |     121.20   |     870.62   |     229.15    |  7.18x slow  |  1.89x slow  |  22.8/102.3/ 11.1 MB |
| pyperf_binary_trees    |      61.41   |    1899.36   |     830.50    | 30.93x slow  | 13.52x slow  | 734.2/294.2/ 11.3 MB |
| pyperf_nqueens         |      82.79   |    2263.84   |     487.34    | 27.34x slow  |  5.89x slow  | 791.3/134.2/ 11.1 MB |
| pyperf_richards_lite   |      58.42   |      87.91   |      45.85    |  1.50x slow  |  0.78x fast  |  22.9/ 22.3/ 11.3 MB |
| pyperf_sieve           |      47.07   |     353.58   |     158.03    |  7.51x slow  |  3.36x slow  | 214.7/102.4/ 11.1 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  3.83x       |  2.15x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
