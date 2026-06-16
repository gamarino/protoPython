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
| startup_empty          |      34.64   |      37.94   |         N/A    |  1.10x slow  |     N/A      |  22.9/  N/A/  9.9 MB |
| int_sum_loop           |      28.28   |     181.79   |     174.82    |  6.43x slow  |  6.18x slow  |  57.5/ 57.4/  9.9 MB |
| list_append_loop       |      28.00   |     210.05   |     205.91    |  7.50x slow  |  7.35x slow  |  73.4/ 73.4/ 10.9 MB |
| str_concat_loop        |      27.29   |     192.98   |     271.24    |  7.07x slow  |  9.94x slow  |  57.5/ 73.5/  9.9 MB |
| range_iterate          |      32.19   |     203.07   |     229.04    |  6.31x slow  |  7.11x slow  |  57.5/ 89.5/ 10.0 MB |
| multithread_cpu        |     242.71   |     790.85   |      30.31    |  3.26x slow  |  0.12x fast  | 227.9/ 23.1/ 10.3 MB |
| attr_lookup            |      36.12   |      78.30   |      87.50    |  2.17x slow  |  2.42x slow  |  22.9/ 54.4/ 10.0 MB |
| call_recursion         |      38.17   |      95.71   |      58.81    |  2.51x slow  |  1.54x slow  |  22.8/ 38.2/ 10.0 MB |
| memory_pressure [INFO] |      60.51   |    1963.32   |    1845.79    | 32.44x slow  | 30.50x slow  | 982.9/1014.3/ 10.2 MB |
| pyperf_fib             |     103.02   |     948.55   |     237.67    |  9.21x slow  |  2.31x slow  |  22.9/102.5/ 10.1 MB |
| pyperf_binary_trees    |      43.29   |      75.65   |     690.59    |  1.75x slow  | 15.95x slow  |  22.9/294.4/ 10.3 MB |
| pyperf_nqueens         |      60.56   |     318.34   |     426.25    |  5.26x slow  |  7.04x slow  |  23.3/134.6/ 10.2 MB |
| pyperf_richards_lite   |      31.55   |      26.56   |      40.07    |  0.84x fast  |  1.27x slow  |  23.0/ 22.3/ 10.1 MB |
| pyperf_sieve           |      31.30   |     224.07   |     138.28    |  7.16x slow  |  4.42x slow  | 150.9/102.5/ 10.2 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  3.66x       |  3.37x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
