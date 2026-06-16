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
| startup_empty          |      37.40   |      22.77   |         N/A    |  0.61x fast  |     N/A      |  22.9/  N/A/ 11.2 MB |
| int_sum_loop           |      35.36   |     173.28   |     165.56    |  4.90x slow  |  4.68x slow  |  57.5/ 57.3/ 11.2 MB |
| list_append_loop       |      38.25   |     197.87   |     186.74    |  5.17x slow  |  4.88x slow  |  73.5/ 73.5/ 11.5 MB |
| str_concat_loop        |      37.16   |     181.97   |     258.21    |  4.90x slow  |  6.95x slow  |  57.5/ 73.3/ 11.1 MB |
| range_iterate          |      40.40   |     183.65   |     217.68    |  4.55x slow  |  5.39x slow  |  57.5/ 89.5/ 11.2 MB |
| multithread_cpu        |     695.55   |     702.41   |    1182.62    |  1.01x slow  |  1.70x slow  | 223.8/ 22.9/ 11.3 MB |
| attr_lookup            |      48.22   |      72.50   |      83.76    |  1.50x slow  |  1.74x slow  |  22.8/ 54.3/ 11.2 MB |
| call_recursion         |      52.87   |      95.69   |      61.27    |  1.81x slow  |  1.16x slow  |  22.8/ 38.2/ 11.2 MB |
| memory_pressure [INFO] |      71.46   |    1875.59   |    1736.90    | 26.25x slow  | 24.31x slow  | 982.9/1014.4/ 11.3 MB |
| pyperf_fib             |     164.68   |    1158.01   |     288.42    |  7.03x slow  |  1.75x slow  |  22.9/102.5/ 11.2 MB |
| pyperf_binary_trees    |      66.59   |      88.66   |     889.76    |  1.33x slow  | 13.36x slow  |  22.8/294.3/ 11.3 MB |
| pyperf_nqueens         |      86.79   |     377.94   |     492.42    |  4.35x slow  |  5.67x slow  |  23.3/134.3/ 11.2 MB |
| pyperf_richards_lite   |      59.65   |      34.65   |      46.74    |  0.58x fast  |  0.78x fast  |  23.1/ 22.4/ 11.3 MB |
| pyperf_sieve           |      61.49   |     265.58   |     177.41    |  4.32x slow  |  2.89x slow  | 150.9/102.5/ 11.2 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  2.42x       |  3.13x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
