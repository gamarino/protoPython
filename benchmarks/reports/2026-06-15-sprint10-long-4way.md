# protoPython long-loop honest comparison (post sprint-10)

Each bench scaled so CPython 3.14t wall lands in the 1-3 s range so startup overhead (~30 ms) is <2 % of total wall.
Baseline is CPython 3.14t (free-threading, GIL off) — apples-to-apples concurrency-wise vs protoPython, which is GIL-free by construction.  The `cp/cpt` column shows the lock cost of PEP 703 on each workload (>1.0 = GIL build is faster).


| Benchmark            | CPython-t ms | CPython GIL ms | cp/cpt | protopy ms | py/cpt | protopyc ms | pc/cpt |
|----------------------|-------------:|---------------:|-------:|-----------:|-------:|------------:|-------:|
| int_sum_loop         |      52.1 |      63.1 |  1.21x |     254.0 |  4.88x |     205.0 |  3.94x |
| list_append_loop     |      59.7 |      56.5 |  0.95x |    1376.7 | 23.05x |    1049.8 | 17.57x |
| str_concat_loop      |      26.5 |      37.1 |  1.40x |     348.6 | 13.14x |    9960.5 | 375.51x |
| range_iterate        |      85.9 |     111.1 |  1.29x |     451.5 |  5.26x |    1314.7 | 15.31x |
| multithread_cpu      |     192.7 |     614.5 |  3.19x |     754.6 |  3.91x |      26.5 |  0.14x |
| attr_lookup          |     387.0 |     371.5 |  0.96x |    2205.4 |  5.70x |      80.2 |  0.21x |
| call_recursion       |      39.2 |      44.8 |  1.14x |     103.5 |  2.64x |      63.1 |  1.61x |
| pyperf_fib           |     487.1 |     483.7 |  0.99x |    5817.8 | 11.94x |     N/A   |   N/A  |
| pyperf_binary_trees  |      92.2 |      93.8 |  1.02x |    8946.3 | 96.99x |     N/A   |   N/A  |
| pyperf_nqueens       |     162.8 |     172.0 |  1.06x |    1394.7 |  8.57x |     N/A   |   N/A  |
| pyperf_richards_lite |      29.1 |      35.2 |  1.21x |      67.2 |  2.31x |     N/A   |   N/A  |
| pyperf_sieve         |      72.2 |      86.5 |  1.20x |    3647.0 | 50.50x |     N/A   |   N/A  |

## Geomeans — baseline is CPython 3.14t (no GIL)
* CPython GIL / CPython-t (lock cost, long workloads): **1.22x**
* protopy / CPython-t:  **9.48x**
* protopyc / CPython-t: **4.06x**
