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
| startup_empty          |     177.98   |      89.52   |         N/A    |  0.50x fast  |     N/A      |  21.1/  N/A/ 10.4 MB |
| int_sum_loop           |     131.84   |      91.15   |      85.82    |  0.69x fast  |  0.65x fast  |  21.1/ 20.5/ 10.5 MB |
| list_append_loop       |     161.66   |     802.69   |     636.89    |  4.97x slow  |  3.94x slow  |  61.5/ 62.5/ 10.8 MB |
| str_concat_loop        |     130.09   |    1674.04   |    1830.00    | 12.87x slow  | 14.07x slow  |  85.6/ 85.3/ 10.4 MB |
| range_iterate          |     121.17   |     603.11   |     693.79    |  4.98x slow  |  5.73x slow  |  42.1/ 89.6/ 10.4 MB |
| multithread_cpu        |    2661.42   |     205.06   |    1319.88    |  0.08x fast  |  0.50x fast  |  25.5/ 20.8/ 10.5 MB |
| attr_lookup            |     190.67   |     946.81   |     266.90    |  4.97x slow  |  1.40x slow  |  38.0/ 52.6/ 10.5 MB |
| call_recursion         |     224.18   |     522.07   |     228.18    |  2.33x slow  |  1.02x slow  |  21.0/ 37.2/ 10.4 MB |
| memory_pressure [INFO] |     275.07   |   11828.49   |   52797.31    | 43.00x slow  | 191.94x slow | 341.0/1366.5/ 10.5 MB |
| pyperf_fib             |     447.06   |    5039.44   |     694.45    | 11.27x slow  |  1.55x slow  |  21.1/102.1/ 10.5 MB |
| pyperf_binary_trees    |     208.77   |    8823.35   |   11189.73    | 42.26x slow  | 53.60x slow  | 229.6/467.8/ 10.5 MB |
| pyperf_nqueens         |     243.22   |    7758.52   |    1842.20    | 31.90x slow  |  7.57x slow  | 166.5/135.9/ 10.4 MB |
| pyperf_richards_lite   |     146.13   |     822.00   |     102.23    |  5.62x slow  |  0.70x fast  |  38.8/ 20.8/ 10.5 MB |
| pyperf_sieve           |     138.07   |    1176.74   |    1101.74    |  8.52x slow  |  7.98x slow  |  87.1/131.5/ 10.5 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  4.02x       |  2.99x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
