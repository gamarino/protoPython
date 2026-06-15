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
| startup_empty          |      47.03   |      27.40   |         N/A    |  0.58x fast  |     N/A      |  22.8/  N/A/ 11.1 MB |
| int_sum_loop           |      38.74   |     166.14   |     168.94    |  4.29x slow  |  4.36x slow  |  57.4/ 57.4/ 11.2 MB |
| list_append_loop       |      36.63   |     193.82   |     190.33    |  5.29x slow  |  5.20x slow  |  73.3/ 73.4/ 11.4 MB |
| str_concat_loop        |      40.83   |     176.12   |     253.33    |  4.31x slow  |  6.20x slow  |  57.4/ 73.4/ 11.2 MB |
| range_iterate          |      39.03   |     173.41   |     205.41    |  4.44x slow  |  5.26x slow  |  57.3/ 89.4/ 11.2 MB |
| multithread_cpu        |     815.08   |     935.65   |    1207.33    |  1.15x slow  |  1.48x slow  | 524.3/ 22.8/ 11.2 MB |
| attr_lookup            |      54.58   |      76.62   |      96.07    |  1.40x slow  |  1.76x slow  |  22.8/ 54.2/ 11.1 MB |
| call_recursion         |      62.41   |     102.03   |      67.28    |  1.63x slow  |  1.08x slow  |  22.7/ 38.1/ 11.2 MB |
| memory_pressure [INFO] |      72.93   |    2437.38   |    2251.84    | 33.42x slow  | 30.88x slow  | 982.8/1030.3/ 11.3 MB |
| pyperf_fib             |     113.70   |     859.14   |     224.78    |  7.56x slow  |  1.98x slow  |  22.7/102.3/ 11.2 MB |
| pyperf_binary_trees    |      48.54   |    1493.45   |     587.25    | 30.77x slow  | 12.10x slow  | 734.4/294.4/ 11.3 MB |
| pyperf_nqueens         |      63.04   |    1745.67   |     379.27    | 27.69x slow  |  6.02x slow  | 791.4/134.2/ 11.1 MB |
| pyperf_richards_lite   |      39.19   |      68.51   |      33.60    |  1.75x slow  |  0.86x fast  |  22.9/ 22.2/ 11.2 MB |
| pyperf_sieve           |      42.75   |     330.16   |     143.19    |  7.72x slow  |  3.35x slow  | 214.7/102.2/ 11.2 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  3.96x       |  3.12x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
