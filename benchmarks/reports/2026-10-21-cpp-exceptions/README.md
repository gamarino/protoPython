# protoPython with C++ exceptions — 2026-10-21

`perf/cpp-exceptions`, Release, protoCore 3.0.0 (`cf52f9f5`): a Python
exception is a C++ exception (`PyException`), a Python `try` a C++ `try`, no
pending flag polled per opcode. Baseline: `../2026-10-21-pre-exceptions/`
(`perf/fast-paths` at `df185d1a`), measured the same way.

## Suite

`run.sh`: `benchmarks/run_4way_interleaved.py`, three rounds, each behind the
quiet gate (1.57-1.80 busy CPUs; the baseline run had 1.23-2.16) on a fresh
copy of the library. Medians of the three rounds, ratio to CPython 3.14t
(> 1: protoPython slower).

| Benchmark | CPython 3.14t ms | protopy ms | protopy ratio | protopyc ms | protopyc ratio | before: protopy / protopyc |
|---|---:|---:|---:|---:|---:|---:|
| `startup_empty` | 23.0 | 20.0 | 0.88x | — | — | 0.91x / — |
| `int_sum_loop` | 498.2 | 467.8 | 0.94x | 295.1 | 0.59x | 1.03x / 0.61x |
| `list_append_loop` | 24.7 | 142.7 | 5.77x | 159.2 | 6.48x | 6.15x / 6.80x |
| `str_concat_loop` | 23.7 | 127.2 | 5.47x | 181.7 | 7.68x | 5.63x / 7.90x |
| `range_iterate` | 314.8 | 500.4 | 1.57x | 329.1 | 1.01x | 1.80x / 1.07x |
| `multithread_cpu` | 187.0 | 96.7 | 0.52x | 52.5 | 0.28x | 0.59x / 0.25x |
| `attr_lookup` | 365.4 | 1588.7 | 4.40x | 832.2 | 2.28x | 4.57x / 2.37x |
| `call_recursion` | 34.0 | 192.4 | 5.51x | 157.6 | 4.64x | 5.50x / 4.61x |
| `memory_pressure` (informational) | 54.4 | 1479.5 | 28.20x | 1531.4 | 29.03x | 27.62x / 28.57x |
| `pyperf_fib` | 96.5 | 445.4 | 4.65x | — | — | 4.66x / — |
| `pyperf_binary_trees` | 35.4 | 249.9 | 6.81x | — | — | 14.21x / — |
| `pyperf_nqueens` | 52.2 | 112.6 | 2.16x | — | — | 2.32x / — |
| `pyperf_richards_lite` | 24.5 | 36.6 | 1.49x | — | — | 1.56x / — |
| `pyperf_sieve` | 27.9 | 94.7 | 3.29x | — | — | 3.48x / — |
| **Geomean** (`memory_pressure` excluded) | | | **2.53x** | | **1.90x** | 2.82x / 1.92x |

## Exceptions

The probes of `../2026-10-21-pre-exceptions/exception_probes/`, `perf stat -r 3`
cycles per iteration:

| Probe | before | now | CPython 3.14t |
|---|---:|---:|---:|
| `empty_loop` | 139 | 116 | 175 |
| `try_no_raise` | 238 | 198 | 171 |
| `raise_catch` | 192,657 | 108,671 | 1,052 |
| `raise_prebuilt` (raise of an existing exception) | 32,446 | 27,188 | 1,624 |
| `keyerror` | 205,103 | 114,071 | 1,110 |
| `hasattr_miss` | 11,441 | 10,865 | 662 |
| `gen_exhaust` | 397,840 | 260,484 | 929 |
| `construct_plain` (an instance of a plain class) | 22,345 | 12,730 | 998 |

The normal path no longer pays for exceptions: the empty loop runs faster
than CPython 3.14t, a `try` that does not raise costs 82 cycles over it. A
raise still costs about 100 times CPython's. A C++ throw / catch across two
small frames costs 5,300 cycles on this machine, the floor of this design;
the rest is the exception object (an instantiation through the generic call
path), its traceback link, and unwinding through the dispatch function,
which is about 118 KB of code (GCC's personality routine scans its
call-site table linearly). Splitting the dispatch into per-opcode functions
is the next step for both.

Call overhead: against the baseline, `pyperf_fib` executes 4.5 % more
instructions (about 100 per call: the activation is now a struct whose
levels are member functions, after closures capturing it by reference cost
270 instructions a call), at an unchanged CPython ratio in the suite above.
