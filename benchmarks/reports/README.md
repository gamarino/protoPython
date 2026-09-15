# Benchmark reports

Each file in this directory is a dated snapshot of one benchmark run, named by
date. Reports are kept as they were written; the only later additions are dated
notes at the top of reports whose figures are known to be invalid or whose title
is misleading.

Workloads, CPython baselines and harnesses changed between reports. For example,
`multithread_cpu` was scaled up in commit 5c2b4b69, the Python micro-benchmarks
were resized to match protoCpp in commit 3d425957, and the CPython reference moved
from the system interpreter to CPython 3.14 builds with and without the GIL.
Compare figures within one report, not across reports.

## How the numbers are produced

The harness scripts are in [`benchmarks/`](..):

| Script | Columns | Measurement |
|--------|---------|-------------|
| [`run_benchmarks.py`](../run_benchmarks.py) | CPython, protopy, protopyc | Wall-clock of each child process (interpreter start-up included) and peak RSS from `/usr/bin/time`; median of 5 runs after 2 warm-up runs. Writes the "protoPython performance audit — *date*" reports. Versions before commit f8117a35 had no protopyc column and printed the box-drawn "Performance Audit: protoPython vs CPython 3.14" tables. |
| [`run_4way_interleaved.py`](../run_4way_interleaved.py) | CPython 3.14 (GIL on), CPython 3.14t (free-threading), protopy, protopyc | Wall-clock per process; all binaries run interleaved (2 warm-up runs each, then 5 rounds); ratios relative to CPython 3.14t. |
| [`run_long_4way.py`](../run_long_4way.py) | Same as above | Workloads scaled through `BENCH_N` so that start-up is a small share of wall-clock; ratios relative to CPython 3.14t. |
| [`run_full_stack.py`](../run_full_stack.py) | protoCpp binaries (`cpp`, `proto`, `proto_fast`), protopy, protopyc, CPython 3.14 with and without the GIL | Python columns use the inner time each benchmark prints on its `BENCH_RESULT ... ms=` line (start-up excluded); protoCpp columns use wall-clock. Median of 3 rounds after 1 warm-up. It expects protoCpp as a sibling checkout (`../protoCpp/build_release`); set the `PROTOCPP_BUILD` environment variable to use another build. |

protopyc columns compile each script with `protopyc --build-so` and load the
result with `run_module`. The binaries are passed through environment variables:
`PROTOPY_BIN`, `PROTOPYC_BIN`, `RUN_MODULE_BIN`, `CPYTHON_BIN` and, for the
four-way and full-stack scripts, `CPYTHONT_BIN`; `LD_LIBRARY_PATH_BENCH` adds
library search paths. For example:

```bash
PROTOPY_BIN=... PROTOPYC_BIN=... RUN_MODULE_BIN=... \
CPYTHON_BIN=... CPYTHONT_BIN=... \
python3 benchmarks/run_4way_interleaved.py --output report.md
```

Since commit e0d96c9b, `run_benchmarks.py` and the four-way scripts drop runs
that time out, exit with a non-zero status or, for benchmarks with an expected
output pattern, print an unexpected last line. Reports written earlier had no such
validation. Rows tagged `[INFO]` are shown but excluded from that report's geomean.

## Known measurement problems

### (a) protopyc skipped `main()` in scripts that import `_thread`

In the protopyc builds of mid-June 2026, the `__name__ == "__main__"` check
evaluated to false under `run_module` in scripts that import `_thread`, so
`main()` never ran. The protopyc `multithread_cpu` figures of about 21–33 ms in
the June 2026 reports therefore measure module initialisation, not the workload.
The problem is described at the top of
[2026-06-15-sprint8-4way-honest.md](2026-06-15-sprint8-4way-honest.md) and in
commit e0d96c9b; `multithread_cpu` had no output pattern at that point, so the
new exit-code validation did not catch it. Every affected report carries a note.

The protopyc column of
[2026-06-16-full-stack-cpp-proto-python.md](2026-06-16-full-stack-cpp-proto-python.md)
is invalid for a related reason: two protopyc bugs (the loop target of a `for`
inside a function, and module globals not installed on new threads), fixed later
in commit eab663bd, made three of the six protopyc benchmarks fail silently and
the rest unreliable. That report carries a note as well.

### (b) protopy called a script's `main()` a second time

From commit 287acba5 (2026-02-03) until commit bd6bd4f0 (2026-09-15), protopy
looked up a module-level `main` after executing a script and called it. A script
that calls `main()` itself ran it twice. `int_sum_loop.py`, `list_append_loop.py`,
`str_concat_loop.py`, `range_iterate.py`, `multithreaded_cpu.py` and the
`bench_*.py` scripts in `benchmarks/pyperf/` currently call it under their
`__main__` guard. Every report in this directory predates bd6bd4f0, so protopy
wall-clock and peak-RSS figures for scripts that called `main()` at the time
include that second run. The `BENCH_RESULT ... ms=` inner timings used by
`run_full_stack.py` are printed before the second call and are unaffected. The
reports carry no individual note for this.

### (c) Sprint-9 figures came from silent crashes

Under the sprint-9 build (commit eee05bd7), `pyperf_binary_trees` raised
`AttributeError` after about 88 ms, and the harness, which did not check exit
codes then, recorded those 88 ms as a result. The "2.42×" protopy geomean and the
`binary_trees` "1.33×" figure published from that run (README commit 58b8fb46)
were artefacts. The change and the README update were reverted (b4aec0bf,
ae93dd27), the four sprint-9 reports were deleted (ebb51d87), and commit e0d96c9b
added result validation to the harness. No report in this directory contains
sprint-9 figures: two reports carry "(sprint-9)" in their title only because the
four-way harness hard-codes that heading (see their notes), and the `2.42x`
`call_recursion` ratios in `baseline_2026-04-28.md` and `baseline_2026-05-01.md`
are unrelated measurements.

## Reports, newest first

"Added in" is the commit that added the report to the repository.

| Report | Contents | Added in | Notes |
|--------|----------|----------|-------|
| [2026-06-16-full-stack-cpp-proto-python.md](2026-06-16-full-stack-cpp-proto-python.md) | C++, protoCore from C++ (protoCpp), protopy, protopyc and CPython 3.14 with and without the GIL at the same workload sizes; `run_full_stack.py`. | 3d425957 | protopyc column invalid, see (a) |
| [2026-06-15-sprint10-long-4way.md](2026-06-15-sprint10-long-4way.md) | Long-loop four-way comparison after sprint-10; `run_long_4way.py`. | bc032b22 | protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint10-no-keys-tracking.md](2026-06-15-sprint10-no-keys-tracking.md) | Four-way comparison after sprint-10, where instance attribute writes stop maintaining a per-instance `__keys__` list. | 6368ec16 | Title; protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint8-4way-hardened.md](2026-06-15-sprint8-4way-hardened.md) | Four-way comparison of the sprint-8 build with the harness result validation. | e0d96c9b | Title; protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint8-4way-honest.md](2026-06-15-sprint8-4way-honest.md) | First four-way interleaved comparison: CPython 3.14 with and without the GIL, protopy, protopyc. | e2149f49 | protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint-8-while-scope-fix.md](2026-06-15-sprint-8-while-scope-fix.md) | Three-column run after the compiler fix for assignments inside `while` bodies. | d7dfec38 | protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint-6-resolve-simplification.md](2026-06-15-sprint-6-resolve-simplification.md) | Three-column run after removing a string-comparison quick path from name resolution. | 5bc4d228 | protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint-5-attrcache-fix-rerun.md](2026-06-15-sprint-5-attrcache-fix-rerun.md) | Second three-column run after a protoCore attribute-cache fix. | 0187bb07 | |
| [2026-06-15-sprint-5-attrcache-fix.md](2026-06-15-sprint-5-attrcache-fix.md) | First three-column run after the same fix. | 0187bb07 | protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint-4.md](2026-06-15-sprint-4.md) | Three-column run after skipping type-tag checks in `LOAD_ATTR` through the `getType` cache. | 8e50cbe4 | protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint-3.md](2026-06-15-sprint-3.md) | Three-column run after adding a per-thread object-to-type cache in `PythonEnvironment::getType`. | 10ae9596 | protopyc `multithread_cpu`, see (a) |
| [2026-06-15-sprint-2.md](2026-06-15-sprint-2.md) | Three-column run after the sprint-2 changes to `LIST_APPEND`, the `LOAD_METHOD` inline cache and `str` + `str` concatenation. | e9fd8bef | |
| [2026-06-15-post-optimisation.md](2026-06-15-post-optimisation.md) | Three-column full-suite run. | 358ac368 | protopyc `multithread_cpu`, see (a) |
| [2026-05-24-perf-final.md](2026-05-24-perf-final.md), [2026-05-24-perf-postfix2.md](2026-05-24-perf-postfix2.md), [2026-05-24-perf-postfix.md](2026-05-24-perf-postfix.md) | Three-column runs added with the change that removes `getenv` calls from the interpreter hot path and re-enables the protopyc column. | 45a25c41 | |
| [2026-05-24-perf-snapshot.md](2026-05-24-perf-snapshot.md) | protopy against CPython 3.14.0 free-threading, with commentary; the protopyc column is excluded because that run's protopyc results were load failures. | 45a25c41 | |
| [2026-05-13-three-column-final-geomean.md](2026-05-13-three-column-final-geomean.md) | Three-column run with `memory_pressure` counted in the geomean again. | b8b62edd | |
| [2026-05-13-three-column-safepoint.md](2026-05-13-three-column-safepoint.md) | Three-column run after protopyc began emitting a GC safepoint at the head of each `while`/`for` iteration (commit b159c48e). | 6226bb07 | |
| [2026-05-13-three-column-pyperf.md](2026-05-13-three-column-pyperf.md) | Three-column run that adds the pyperformance subset. | 5cf365ce | |
| [2026-05-13-three-column-mt.md](2026-05-13-three-column-mt.md) | Three-column run with the scaled-up `multithread_cpu` workload. | 5c2b4b69 | |
| [2026-05-13-three-column-postreturn.md](2026-05-13-three-column-postreturn.md) | Three-column run after bulk argument-list allocation and `return` instead of `throw` for function returns in protopyc. | 10ecd26c | |
| [2026-05-13-three-column-final.md](2026-05-13-three-column-final.md) | Three-column run after the protopyc direct self-recursion call and SmallInt inline fast paths. | bc457a55 | |
| [2026-05-13-three-column-postfastpath.md](2026-05-13-three-column-postfastpath.md) | Another post-fast-path three-column run, committed later with a working-tree snapshot. | 832d67db | |
| [2026-05-13-three-column.md](2026-05-13-three-column.md) | First three-column run, when the protopyc column was added to the harness. | f8117a35 | |
| [2026-05-12-compiled-vs-cpython.md](2026-05-12-compiled-vs-cpython.md) | Manual measurement of four expression-level workloads under CPython, protopy and protopyc. | 70c12c39 | |
| [2026-05-12-prebench.md](2026-05-12-prebench.md) | Two-column run of protopy against CPython 3.14, committed later with a working-tree snapshot. | 832d67db | |
| [baseline_2026-05-05.md](baseline_2026-05-05.md) | Two-column baseline. | 57f00967 | |
| [baseline_2026-05-01-final.md](baseline_2026-05-01-final.md) | Two-column baseline at the end of the attribute-cache work. | 7905e048 | |
| [baseline_2026-05-01-cache-redesign.md](baseline_2026-05-01-cache-redesign.md) | Two-column baseline after the protoCore attribute-cache redesign. | 553de31d | |
| [baseline_2026-05-01.md](baseline_2026-05-01.md) | Two-column baseline after protoCore cache optimisations. | ad68eac8 | |
| [baseline_2026-04-28.md](baseline_2026-04-28.md) | Two-column baseline after rebaselining against the 2026-04-28 suite. | d95af03b | |
