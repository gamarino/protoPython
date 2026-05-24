# protoPython performance audit — 2026-05-24

Platform: Linux x86_64, median of 5 runs (timeouts excluded).

Three execution modes per workload:
* **CPython** — the system interpreter (reference).
* **protopy** — protoPython bytecode interpreter.
* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.

Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.
Rows tagged `[INFO]` are reported for transparency but do NOT participate in the geomean — see the report footer.

| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| startup_empty          |      36.46   |      22.40   |         N/A    |  0.61x fast  |     N/A      |  21.5/  N/A/ 10.6 MB |
| int_sum_loop           |      31.46   |      22.57   |      20.94    |  0.72x fast  |  0.67x fast  |  21.2/ 20.9/ 10.6 MB |
| list_append_loop       |      32.82   |     265.96   |     162.48    |  8.10x slow  |  4.95x slow  |  88.0/ 72.1/ 11.0 MB |
| str_concat_loop        |      29.07   |     291.00   |     225.95    | 10.01x slow  |  7.77x slow  |  72.0/ 72.1/ 10.8 MB |
| range_iterate          |      32.81   |     153.88   |     176.10    |  4.69x slow  |  5.37x slow  |  56.0/ 88.0/ 10.8 MB |
| multithread_cpu        |     541.08   |     964.19   |    1144.42    |  1.78x slow  |  2.12x slow  | 686.2/ 21.2/ 10.8 MB |
| attr_lookup            |      38.09   |     208.89   |      73.91    |  5.48x slow  |  1.94x slow  |  53.4/ 53.0/ 10.6 MB |
| call_recursion         |      38.81   |     115.56   |      50.69    |  2.98x slow  |  1.31x slow  |  21.4/ 36.9/ 10.6 MB |
| memory_pressure [INFO] |      53.86   |    2687.11   |    1587.15    | 49.90x slow  | 29.47x slow  | 1061.5/1029.1/ 10.9 MB |
| pyperf_fib             |      89.25   |    1259.14   |     204.63    | 14.11x slow  |  2.29x slow  |  21.5/101.1/ 10.8 MB |
| pyperf_binary_trees    |      40.96   |    1936.99   |     861.41    | 47.30x slow  | 21.03x slow  | 783.1/373.2/ 10.9 MB |
| pyperf_nqueens         |      49.53   |    1802.28   |     374.91    | 36.39x slow  |  7.57x slow  | 790.2/133.1/ 10.8 MB |
| pyperf_richards_lite   |      33.46   |     197.12   |      33.15    |  5.89x slow  |  0.99x fast  |  55.0/ 21.0/ 10.9 MB |
| pyperf_sieve           |      31.45   |     315.94   |     120.39    | 10.04x slow  |  3.83x slow  | 213.5/101.1/ 10.8 MB |
|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|
| Geomean (n=13)         |              |              |               |  5.72x       |  3.17x       |                     |

`[INFO]` rows excluded from the geomean:
* **memory_pressure** — protoCore defers GC until the working set forces it (concurrent collector, tiny STW window).  The wall time on this workload reflects collection scheduling under stress, not user-code throughput; the ratio against CPython's reference-counted eager-deallocation model is not comparable apples-to-apples.  Reported for transparency.
