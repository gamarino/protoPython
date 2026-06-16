# Full-stack honest comparison (cpp / protoCore / protoPython / CPython)

Inner-only timing for Python variants (parsed from each bench's
`BENCH_RESULT ms=` marker — excludes startup + GC tail).  Wall-clock
for protoCpp binaries (startup is ~1 ms there).  Same workload size
(matching the protoCpp benches) across all seven columns so the
numbers are directly comparable.

**Baseline 1.0 = CPython 3.14t (free-threading, GIL off).**

Columns:
* `cpp`         — pure C++, hardware floor (no protoCore).
* `proto`       — protoCore directly from C++ (kernel floor).
* `proto_fast`  — protoCore with API-side optimisations.
* `cp` / `cpt`  — CPython 3.14 with GIL on / off (uv builds).
* `protopy`     — protoPython bytecode interpreter.
* `protopyc`    — protoPython AOT to C++.

## Absolute wall-time / inner-time (ms)
| Bench                |        cpp |      proto | proto_fast |         cp |        cpt |    protopy |   protopyc |
|----------------------|------------|------------|------------|------------|------------|------------|------------|
| int_sum_loop         |     4.72 |   105.60 |    56.69 |   413.98 |   541.22 |   672.41 |     N/A  |
| attr_lookup          |     6.80 |   300.94 |     N/A  |   389.44 |   372.59 |  2703.94 |  5022.64 |
| list_append_loop     |     3.20 |    17.50 |     N/A  |     0.66 |     0.96 |    18.73 |    21.89 |
| str_concat_loop      |     2.54 |    14.93 |     N/A  |     0.15 |     0.09 |     4.09 |    91.62 |
| call_recursion       |     1.49 |    34.74 |    15.43 |     9.40 |    10.16 |    74.09 |    34.24 |
| multithread_cpu      |     3.68 |    35.33 |    26.19 |   563.88 |   160.63 |   307.03 |     0.74 |

## Ratios vs CPython 3.14t (no GIL)
| Bench                |      cpp |    proto | proto_fast |       cp |      cpt |  protopy | protopyc |
|----------------------|----------|----------|----------|----------|----------|----------|----------|
| int_sum_loop         |  0.01x |  0.20x |  0.10x |  0.76x |  1.00x |  1.24x |  N/A  |
| attr_lookup          |  0.02x |  0.81x |  N/A  |  1.05x |  1.00x |  7.26x | 13.48x |
| list_append_loop     |  3.34x | 18.23x |  N/A  |  0.69x |  1.00x | 19.51x | 22.80x |
| str_concat_loop      | 28.20x | 165.84x |  N/A  |  1.67x |  1.00x | 45.44x | 1017.99x |
| call_recursion       |  0.15x |  3.42x |  1.52x |  0.93x |  1.00x |  7.29x |  3.37x |
| multithread_cpu      |  0.02x |  0.22x |  0.16x |  3.51x |  1.00x |  1.91x |  0.00x |

## Geomeans (baseline = cpythont = 1.0)
* `cpp` (cpp): **0.19x**
* `proto` (proto): **2.67x**
* `proto_fast` (proto_fast): **0.30x**
* `cp` (cpython): **1.20x**
* `cpt` (cpythont): **1.00x**
* `protopy` (protopy): **6.94x**
* `protopyc` (protopyc): **5.47x**
