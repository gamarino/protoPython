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
| startup_empty          |      40.19   |      25.06   |         N/A    |  0.62x fast  |     N/A      |  23.2/  N/A/ 11.2 MB |
| int_sum_loop           |      37.57   |      26.48   |      21.15    |  0.70x fast  |  0.56x fast  |  22.9/ 22.5/ 11.2 MB |
| list_append_loop       |      38.87   |     322.58   |     220.67    |  8.30x slow  |  5.68x slow  |  89.9/ 73.8/ 11.4 MB |
| str_concat_loop        |      39.94   |     354.29   |     266.51    |  8.87x slow  |  6.67x slow  |  74.0/ 73.9/ 11.2 MB |
| range_iterate          |      47.85   |     192.48   |     230.73    |  4.02x slow  |  4.82x slow  |  57.9/ 89.8/ 11.2 MB |
| multithread_cpu        |    1066.02   |    1115.41   |      30.85    |  1.05x slow  |  0.03x fast  | 447.8/ 22.8/ 11.2 MB |
| attr_lookup            |      51.71   |     205.20   |      87.27    |  3.97x slow  |  1.69x slow  |  55.1/ 54.6/ 11.2 MB |
| call_recursion         |      56.91   |      96.75   |      58.93    |  1.70x slow  |  1.04x slow  |  22.9/ 38.5/ 11.1 MB |
| memory_pressure [INFO] |      92.05   |    4009.75   |    2639.18    | 43.56x slow  | 28.67x slow  | 1063.0/1030.7/ 11.3 MB |
| pyperf_fib             |     161.41   |    1230.62   |     316.59    |  7.62x slow  |  1.96x slow  |  23.0/102.7/ 11.2 MB |
| pyperf_binary_trees    |      75.00   |    2690.65   |    1332.98    | 35.87x slow  | 17.77x slow  | 784.9/374.7/ 11.2 MB |
| pyperf_nqueens         |      69.33   |    1896.27   |     425.25    | 27.35x slow  |  6.13x slow  | 791.8/134.6/ 11.2 MB |
| pyperf_richards_lite   |      39.14   |     198.25   |      37.54    |  5.07x slow  |  0.96x fast  |  56.7/ 22.7/ 11.1 MB |
| pyperf_sieve           |      39.95   |     341.28   |     131.02    |  8.54x slow  |  3.28x slow  | 215.1/102.6/ 11.2 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  4.49x       |  1.97x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
