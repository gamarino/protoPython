# protoPython before the C++-exceptions change — 2026-10-21

`perf/fast-paths` at `df185d1a` (protoCore 3.0.0 migration plus the
one-lookup attribute reads and writes), Release, protoCore 3.0.0
(`cf52f9f5`). The baseline for replacing the pending-exception flag with C++
exceptions.

## Suite

`run.sh`: `benchmarks/run_4way_interleaved.py`, three rounds, each behind the
quiet gate and on a fresh copy of the library; `py_r*.md` are the rounds,
`quiet_r*.txt` the gate. Round 2 ran with 2.16 busy CPUs (the other two 1.23 /
1.28; the 2026-10-20 run had 1.1-1.3), and absolute times are higher than on
2026-10-20 for CPython too: compare ratios. Medians of the three rounds, ratio
to CPython 3.14t (> 1: protoPython slower); last column, the 2026-10-20 run on
protoCore 2.14.8 (README, "Performance").

| Benchmark | CPython 3.14t ms | protopy ms | protopy ratio | protopyc ms | protopyc ratio | 2.14.8: protopy / protopyc |
|---|---:|---:|---:|---:|---:|---:|
| `startup_empty` | 22.6 | 20.5 | 0.91x | — | — | 0.97x / — |
| `int_sum_loop` | 496.1 | 510.1 | 1.03x | 304.7 | 0.61x | 1.03x / 0.61x |
| `list_append_loop` | 23.7 | 145.5 | 6.15x | 160.6 | 6.80x | 6.14x / 6.91x |
| `str_concat_loop` | 22.9 | 127.0 | 5.63x | 179.7 | 7.90x | 5.85x / 8.89x |
| `range_iterate` | 304.0 | 556.9 | 1.80x | 325.6 | 1.07x | 1.83x / 1.08x |
| `multithread_cpu` | 193.1 | 115.5 | 0.59x | 48.8 | 0.25x | 0.65x / 0.25x |
| `attr_lookup` | 360.4 | 1653.1 | 4.57x | 850.4 | 2.37x | 6.10x / 3.22x |
| `call_recursion` | 33.4 | 183.9 | 5.50x | 155.2 | 4.61x | 5.64x / 4.76x |
| `memory_pressure` (informational) | 53.8 | 1531.6 | 27.62x | 1537.4 | 28.57x | 28.9x / 31.5x |
| `pyperf_fib` | 90.4 | 413.9 | 4.66x | — | — | 4.58x / — |
| `pyperf_binary_trees` | 34.4 | 482.1 | 14.21x | — | — | 14.64x / — |
| `pyperf_nqueens` | 52.8 | 120.1 | 2.32x | — | — | 2.41x / — |
| `pyperf_richards_lite` | 24.8 | 38.6 | 1.56x | — | — | 1.62x / — |
| `pyperf_sieve` | 26.8 | 94.5 | 3.48x | — | — | 3.64x / — |
| **Geomean** (`memory_pressure` excluded) | | | **2.82x** | | **1.92x** | 2.97x / 2.05x |

## Exceptions

`exception_probes/`: each loop prints its result, checked equal to CPython's.
`perf stat -r 3`, cycles per iteration and total time:

| Probe | protopy cycles/it | protopy ms | CPython 3.14t cycles/it | 3.14t ms |
|---|---:|---:|---:|---:|
| `empty_loop` (2,000,000) | 139 | 78 | 171 | 100 |
| `try_no_raise` (2,000,000) | 238 | 136 | 175 | 104 |
| `raise_catch` (200,000) | 192,657 | 10,119 | 1,064 | 63 |
| `keyerror` (200,000) | 205,103 | 11,535 | 1,143 | 70 |
| `hasattr_miss` (200,000) | 11,441 | 735 | 655 | 38 |
| `gen_exhaust` (200,000) | 397,840 | 22,028 | 992 | 65 |

A raise costs 180x CPython's; a `try` that does not raise costs 99 cycles an
iteration over the empty loop (CPython: 4). Counted under gdb (400 against
200 iterations of `raise_catch`): each raise allocates 156 cells and makes 42
`setAttribute` calls. The exception object is built by attribute writes on a
mutable object, and those writes and the collections their garbage triggers
are the cost; the propagation mechanism barely appears in the profile.
