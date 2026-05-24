# protoPython performance audit — 2026-05-24

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |      31.75   |      20.61   |         N/A    |  0.65x fast  |     N/A      |  21.1/  N/A/ 10.6 MB |
| int_sum_loop           |      34.89   |      23.51   |    1193.82    |  0.67x fast  | 34.22x slow  |  21.0/ 19.5/ 10.8 MB |
| list_append_loop       |      38.54   |     309.31   |    1201.57    |  8.03x slow  | 31.18x slow  |  87.9/ 19.5/ 10.9 MB |
| str_concat_loop        |      39.61   |     358.42   |    1176.63    |  9.05x slow  | 29.71x slow  |  72.0/ 19.6/ 10.6 MB |
| range_iterate          |      52.55   |     184.95   |    1184.53    |  3.52x slow  | 22.54x slow  |  56.0/ 19.6/ 10.8 MB |
| multithread_cpu        |     651.46   |    1481.50   |    1171.24    |  2.27x slow  |  1.80x slow  | 606.0/ 19.6/ 10.8 MB |
| attr_lookup            |      41.63   |     213.14   |    1145.45    |  5.12x slow  | 27.52x slow  |  53.4/ 19.5/ 10.8 MB |
| call_recursion         |      39.55   |     118.47   |    1143.26    |  3.00x slow  | 28.91x slow  |  21.2/ 19.6/ 10.6 MB |
| memory_pressure        |      53.85   |    3194.73   |    1145.46    | 59.33x slow  | 21.27x slow  | 1061.2/ 19.6/ 10.9 MB |
| pyperf_fib             |      90.90   |    1250.94   |    1145.67    | 13.76x slow  | 12.60x slow  |  21.4/ 19.6/ 10.6 MB |
| pyperf_binary_trees    |      40.78   |    1971.90   |    1155.87    | 48.36x slow  | 28.34x slow  | 783.0/ 19.6/ 10.9 MB |
| pyperf_nqueens         |      51.17   |    2121.43   |    1145.53    | 41.46x slow  | 22.39x slow  | 790.1/ 19.5/ 10.6 MB |
| pyperf_richards_lite   |      32.51   |     215.03   |    1144.43    |  6.61x slow  | 35.20x slow  |  54.9/ 19.6/ 10.8 MB |
| pyperf_sieve           |      34.17   |     343.34   |    1181.88    | 10.05x slow  | 34.59x slow  | 213.4/ 19.6/ 10.6 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=14)         |              |              |               |  6.77x       | 21.51x       |                     |
