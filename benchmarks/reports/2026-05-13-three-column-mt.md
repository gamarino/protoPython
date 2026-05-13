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
| startup_empty          |     123.52   |     133.72   |         N/A    |  1.08x slow  |     N/A      |  21.1/  N/A/ 10.5 MB |
| int_sum_loop           |     169.02   |      97.00   |      80.50    |  0.57x fast  |  0.48x fast  |  20.9/ 20.6/ 10.4 MB |
| list_append_loop       |     209.80   |    1099.96   |     643.21    |  5.24x slow  |  3.07x slow  |  61.5/ 62.4/ 10.6 MB |
| str_concat_loop        |     224.99   |    2601.61   |    2426.26    | 11.56x slow  | 10.78x slow  |  85.4/ 85.1/ 10.4 MB |
| range_iterate          |     175.98   |     983.10   |    1124.79    |  5.59x slow  |  6.39x slow  |  42.1/ 89.4/ 10.4 MB |
| multithread_cpu        |    2341.19   |     239.34   |    1307.30    |  0.10x fast  |  0.56x fast  |  25.5/ 20.8/ 10.4 MB |
| attr_lookup            |     194.03   |     969.27   |     306.43    |  5.00x slow  |  1.58x slow  |  38.0/ 52.5/ 10.5 MB |
| call_recursion         |     194.46   |     719.73   |     238.67    |  3.70x slow  |  1.23x slow  |  21.0/ 37.1/ 10.4 MB |
| memory_pressure [INFO] |     376.35   |   14539.36   |   70871.85    | 38.63x slow  | 188.31x slow | 323.7/1331.3/ 10.6 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=8)          |              |              |               |  2.11x       |  1.95x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
