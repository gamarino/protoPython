# protoPython performance audit — 2026-05-13

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |     166.85   |      92.82   |         N/A    |  0.56x fast  |     N/A      |  21.0/  N/A/ 10.4 MB |
| int_sum_loop           |     198.02   |     158.12   |     132.73    |  0.80x fast  |  0.67x fast  |  20.9/ 20.5/ 10.5 MB |
| list_append_loop       |     146.89   |    1240.37   |     967.67    |  8.44x slow  |  6.59x slow  |  61.3/ 62.5/ 10.8 MB |
| str_concat_loop        |     132.62   |    1765.18   |    1930.83    | 13.31x slow  | 14.56x slow  |  85.5/ 85.4/ 10.4 MB |
| range_iterate          |     223.14   |     922.41   |     915.41    |  4.13x slow  |  4.10x slow  |  42.0/ 89.5/ 10.5 MB |
| multithread_cpu        |     255.44   |     235.66   |         N/A    |  0.92x fast  |     N/A      |  29.4/  N/A/ 10.5 MB |
| attr_lookup            |     245.31   |     868.31   |     420.75    |  3.54x slow  |  1.72x slow  |  37.9/ 52.5/ 10.5 MB |
| call_recursion         |     231.65   |     441.19   |     246.01    |  1.90x slow  |  1.06x slow  |  21.0/ 37.1/ 10.5 MB |
| memory_pressure        |     331.63   |   14751.12   |   62609.72    | 44.48x slow  | 188.79x slow | 338.5/1366.3/ 10.5 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean vs CPython     |              |              |               |  3.38x       |  5.11x       |                     |
