# protoPython performance audit — 2026-05-24

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |      46.23   |      24.52   |         N/A    |  0.53x fast  |     N/A      |  21.1/  N/A/ 10.6 MB |
| int_sum_loop           |      34.83   |      24.61   |    1183.87    |  0.71x fast  | 33.99x slow  |  21.0/ 19.5/ 10.8 MB |
| list_append_loop       |      40.26   |     288.89   |    1174.84    |  7.18x slow  | 29.18x slow  |  87.9/ 19.6/ 11.0 MB |
| str_concat_loop        |      54.30   |     342.60   |    1202.84    |  6.31x slow  | 22.15x slow  |  72.0/ 19.5/ 10.6 MB |
| range_iterate          |      65.11   |     307.10   |    1244.45    |  4.72x slow  | 19.11x slow  |  56.0/ 19.5/ 10.6 MB |
| multithread_cpu        |     809.30   |    1797.75   |    1160.93    |  2.22x slow  |  1.43x slow  | 714.1/ 19.5/ 10.8 MB |
| attr_lookup            |      39.02   |     233.27   |    1143.39    |  5.98x slow  | 29.30x slow  |  53.2/ 19.6/ 10.6 MB |
| call_recursion         |      40.69   |     121.32   |    1143.24    |  2.98x slow  | 28.10x slow  |  21.2/ 19.6/ 10.6 MB |
| memory_pressure        |      53.45   |    3168.93   |    1148.40    | 59.28x slow  | 21.48x slow  | 1061.4/ 19.6/ 10.9 MB |
| pyperf_fib             |     119.77   |    1920.28   |    1190.34    | 16.03x slow  |  9.94x slow  |  21.2/ 19.6/ 10.8 MB |
| pyperf_binary_trees    |      41.30   |    1990.36   |    1144.40    | 48.19x slow  | 27.71x slow  | 783.1/ 19.6/ 10.8 MB |
| pyperf_nqueens         |      50.99   |    2029.93   |    1157.80    | 39.81x slow  | 22.71x slow  | 790.0/ 19.6/ 10.8 MB |
| pyperf_richards_lite   |      31.90   |     214.67   |    1158.04    |  6.73x slow  | 36.31x slow  |  54.9/ 19.6/ 10.9 MB |
| pyperf_sieve           |      34.80   |     346.87   |    1170.89    |  9.97x slow  | 33.64x slow  | 213.4/ 19.6/ 10.8 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=14)         |              |              |               |  6.73x       | 19.99x       |                     |
