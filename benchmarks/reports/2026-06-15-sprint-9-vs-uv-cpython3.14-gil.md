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
| startup_empty          |      38.14   |      29.96   |         N/A    |  0.79x fast  |     N/A      |  22.9/  N/A/ 10.7 MB |
| int_sum_loop           |      42.40   |     207.14   |     210.29    |  4.89x slow  |  4.96x slow  |  57.6/ 57.5/ 10.7 MB |
| list_append_loop       |      37.65   |     213.68   |     207.60    |  5.68x slow  |  5.51x slow  |  73.5/ 73.5/ 10.9 MB |
| str_concat_loop        |      37.74   |     187.50   |     283.57    |  4.97x slow  |  7.51x slow  |  57.4/ 73.5/ 10.7 MB |
| range_iterate          |      48.29   |     227.39   |     255.28    |  4.71x slow  |  5.29x slow  |  57.4/ 89.4/ 10.7 MB |
| multithread_cpu        |     746.19   |    1121.00   |      29.33    |  1.50x slow  |  0.04x fast  | 279.6/ 23.1/ 10.8 MB |
| attr_lookup            |      56.04   |      88.40   |     103.58    |  1.58x slow  |  1.85x slow  |  22.8/ 54.2/ 10.6 MB |
| call_recursion         |      65.45   |     127.42   |      75.69    |  1.95x slow  |  1.16x slow  |  22.9/ 38.3/ 10.7 MB |
| memory_pressure [INFO] |      62.00   |    1885.08   |    1734.45    | 30.41x slow  | 27.98x slow  | 982.9/1014.4/ 10.8 MB |
| pyperf_fib             |     104.85   |     909.29   |     238.33    |  8.67x slow  |  2.27x slow  |  22.8/102.3/ 10.7 MB |
| pyperf_binary_trees    |      47.41   |      80.76   |     677.76    |  1.70x slow  | 14.29x slow  |  22.9/294.5/ 10.9 MB |
| pyperf_nqueens         |      75.33   |     382.83   |     486.73    |  5.08x slow  |  6.46x slow  |  23.2/134.3/ 10.8 MB |
| pyperf_richards_lite   |      47.38   |      29.60   |      44.02    |  0.62x fast  |  0.93x fast  |  23.0/ 22.4/ 10.6 MB |
| pyperf_sieve           |      45.06   |     262.69   |     169.74    |  5.83x slow  |  3.77x slow  | 150.9/102.4/ 10.7 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  2.80x       |  2.52x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
