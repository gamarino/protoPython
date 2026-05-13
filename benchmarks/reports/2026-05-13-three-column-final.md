# protoPython performance audit — 2026-05-13

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |     225.40   |     143.87   |         N/A    |  0.64x fast  |     N/A      |  21.1/  N/A/ 10.4 MB |
| int_sum_loop           |     133.39   |      73.06   |      91.41    |  0.55x fast  |  0.69x fast  |  21.0/ 20.6/ 10.4 MB |
| list_append_loop       |     139.08   |    1107.55   |     965.41    |  7.96x slow  |  6.94x slow  |  61.5/ 62.2/ 10.6 MB |
| str_concat_loop        |     207.36   |    2805.60   |    1459.75    | 13.53x slow  |  7.04x slow  |  85.5/ 85.5/ 10.4 MB |
| range_iterate          |     196.13   |     971.99   |    1089.03    |  4.96x slow  |  5.55x slow  |  42.1/ 89.4/ 10.4 MB |
| multithread_cpu        |     212.27   |     326.90   |         N/A    |  1.54x slow  |     N/A      |  29.9/  N/A/ 10.5 MB |
| attr_lookup            |     242.75   |    1063.52   |     411.81    |  4.38x slow  |  1.70x slow  |  38.0/ 52.6/ 10.4 MB |
| call_recursion         |     273.36   |     785.01   |    1412.96    |  2.87x slow  |  5.17x slow  |  21.0/ 37.3/ 10.4 MB |
| memory_pressure        |     275.94   |   14114.52   |   62050.37    | 51.15x slow  | 224.87x slow | 340.2/1400.4/ 10.5 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean vs CPython     |              |              |               |  3.85x       |  6.24x       |                     |
