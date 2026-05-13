# protoPython performance audit — 2026-05-13

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |     175.65   |      71.95   |         N/A    |  0.41x fast  |     N/A      |  21.0/  N/A/ 10.5 MB |
| int_sum_loop           |     164.56   |     110.88   |     113.57    |  0.67x fast  |  0.69x fast  |  20.9/ 20.5/ 10.5 MB |
| list_append_loop       |     120.26   |    1002.73   |     716.58    |  8.34x slow  |  5.96x slow  |  61.4/ 62.4/ 10.8 MB |
| str_concat_loop        |     165.67   |    2149.47   |    1674.10    | 12.97x slow  | 10.10x slow  |  85.4/ 85.4/ 10.4 MB |
| range_iterate          |     234.82   |     941.12   |    1090.01    |  4.01x slow  |  4.64x slow  |  42.1/ 89.6/ 10.5 MB |
| multithread_cpu        |     146.42   |     227.31   |         N/A    |  1.55x slow  |     N/A      |  29.8/  N/A/ 10.6 MB |
| attr_lookup            |     301.53   |    1202.00   |     340.24    |  3.99x slow  |  1.13x slow  |  38.0/ 52.6/ 10.4 MB |
| call_recursion         |     257.94   |     357.77   |    1986.99    |  1.39x slow  |  7.70x slow  |  21.0/101.4/ 10.4 MB |
| memory_pressure        |     295.03   |   12780.85   |   54464.87    | 43.32x slow  | 184.61x slow | 341.1/1397.9/ 10.6 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean vs CPython     |              |              |               |  3.28x       |  6.09x       |                     |
