# protoPython performance audit — 2026-05-13

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |     111.10   |      59.64   |         N/A    |  0.54x fast  |     N/A      |  21.1/  N/A/ 10.5 MB |
| int_sum_loop           |     136.48   |      81.63   |      97.59    |  0.60x fast  |  0.72x fast  |  21.0/ 20.6/ 10.4 MB |
| list_append_loop       |     170.43   |    1076.55   |     768.09    |  6.32x slow  |  4.51x slow  |  61.4/ 62.3/ 10.6 MB |
| str_concat_loop        |     195.04   |    1872.59   |    1841.44    |  9.60x slow  |  9.44x slow  |  85.5/ 85.5/ 10.4 MB |
| range_iterate          |     312.71   |    1132.32   |     982.79    |  3.62x slow  |  3.14x slow  |  42.2/ 89.6/ 10.5 MB |
| multithread_cpu        |     247.71   |     293.17   |         N/A    |  1.18x slow  |     N/A      |  37.8/  N/A/ 10.5 MB |
| attr_lookup            |     228.12   |     781.69   |     445.99    |  3.43x slow  |  1.96x slow  |  38.0/ 52.6/ 10.5 MB |
| call_recursion         |     180.19   |     449.23   |    1658.38    |  2.49x slow  |  9.20x slow  |  21.1/ 37.4/ 10.4 MB |
| memory_pressure        |     246.30   |   15575.62   |   55828.74    | 63.24x slow  | 226.67x slow | 323.0/1360.6/ 10.6 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean vs CPython     |              |              |               |  3.29x       |  6.29x       |                     |
