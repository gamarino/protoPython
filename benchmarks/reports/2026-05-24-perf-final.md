# protoPython performance audit — 2026-05-24

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |      45.16   |      28.04   |         N/A    |  0.62x fast  |     N/A      |  21.2/  N/A/ 10.8 MB |
| int_sum_loop           |      49.21   |      30.90   |      26.27    |  0.63x fast  |  0.53x fast  |  21.1/ 20.9/ 10.6 MB |
| list_append_loop       |      49.06   |     376.40   |     231.43    |  7.67x slow  |  4.72x slow  |  88.0/ 71.9/ 10.9 MB |
| str_concat_loop        |      44.86   |     403.42   |     317.38    |  8.99x slow  |  7.08x slow  |  72.0/ 72.0/ 10.6 MB |
| range_iterate          |      51.27   |     207.59   |     195.99    |  4.05x slow  |  3.82x slow  |  56.0/ 88.0/ 10.8 MB |
| multithread_cpu        |     643.29   |    1640.05   |    1170.28    |  2.55x slow  |  1.82x slow  | 662.1/ 21.1/ 10.8 MB |
| attr_lookup            |      47.85   |     233.69   |      87.25    |  4.88x slow  |  1.82x slow  |  53.2/ 52.8/ 10.8 MB |
| call_recursion         |      50.58   |     141.91   |      60.00    |  2.81x slow  |  1.19x slow  |  21.4/ 36.8/ 10.8 MB |
| memory_pressure        |      60.20   |    3399.70   |    1916.34    | 56.47x slow  | 31.83x slow  | 1061.4/1029.0/ 10.9 MB |
| pyperf_fib             |     119.33   |    1673.04   |     260.60    | 14.02x slow  |  2.18x slow  |  21.4/100.9/ 10.8 MB |
| pyperf_binary_trees    |      41.67   |    2004.89   |     969.58    | 48.11x slow  | 23.27x slow  | 783.2/373.0/ 10.9 MB |
| pyperf_nqueens         |      49.98   |    2037.44   |     376.73    | 40.77x slow  |  7.54x slow  | 790.1/133.0/ 10.8 MB |
| pyperf_richards_lite   |      31.62   |     210.07   |      34.95    |  6.64x slow  |  1.11x slow  |  54.9/ 21.0/ 10.9 MB |
| pyperf_sieve           |      34.92   |     331.72   |     125.61    |  9.50x slow  |  3.60x slow  | 213.4/101.0/ 10.8 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=14)         |              |              |               |  6.71x       |  3.53x       |                     |
