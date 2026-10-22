# protoPython on protoCore's unified exception model: final measurement (2026-10-22)

## Builds

| | protoPython | protoCore | Notes |
|---|---|---|---|
| **BEFORE** | `cd43628b` | `cf52f9f5` | `PyException` as a C++ exception; no `ProtoException` |
| **AFTER** | `1633557c` (`perf/cpp-exceptions` tip) | `65760e4a` (`feature/args-calling-convention` tip; code `e64b211f`) | adopts `ProtoException` |
| context only | `8a367f65` (10-21 final) / `df185d1a` (pending flag) | 3.0.0 | [`../2026-10-21-cpp-exceptions-final/`](../2026-10-21-cpp-exceptions-final/) and [`../2026-10-21-pre-exceptions/`](../2026-10-21-pre-exceptions/); a different machine state; not re-measured |

- **Worktrees**: Release builds in clean worktrees under
  `.agent_scratch/meas_1022/{before,after}/{protoCore,protoPython}`.
- **protoCore linkage**
  - Each protoPython is configured with
    `-DCMAKE_DISABLE_FIND_PACKAGE_protoCore=ON`.
  - It builds its sibling `../protoCore` worktree by `add_subdirectory`.
  - The resulting `libprotoCore.so.3.0.0` is byte-identical (md5) to that
    worktree's standalone `build_release`.
- **Library loading**
  - The binaries carry a `RUNPATH`. Every run loaded a fresh copy of its own
    `libprotoCore.so.4`; see `ldd.txt`.
  - protopyc's `--build-so` compiles against its own worktree's headers and
    libraries. Their paths are embedded at configure time.
- **Build and measurement**
  - `build.sh` built everything first, one build at a time.
  - `py_run.sh` then measured, with nothing else running.
- **Quiet gate**: **`QUIET_CORES=4.0`** (`quiet_*.txt`). The default of 2.5
  was unreachable, because the VS Code GPU process took about 1 CPU and
  Chrome 1-2 more. 2.1-3.7 CPUs were busy at the start of the runs.
- **Self-report check** (`py_verify.py` → `verify.txt`)
  - Coverage: all 13 harness benchmarks and all 10 probes.
  - Each ran once with protopy and protopyc, BEFORE and AFTER, and with
    CPython 3.14t.
  - Rule: the printed work, with timings stripped, had to equal CPython's.
  - Result: **115 / 115 OK**.
  - A first pass, `verify_first_pass_chatter_bug.txt`, flagged every
    protopyc run. That was a bug in the checker: `run_module` prints loader
    lines around the program's output. The checker was fixed to drop those
    lines and rerun.

## `benchmarks/run_4way_interleaved.py`

- Three rounds per variant, with the variant order alternated (A,B / B,A /
  A,B).
- Each harness run interleaves protopy, protopyc, CPython and CPython 3.14t
  over 5 rounds after 2 warm-ups.
- The figures are medians of the three per-round medians, in ms.
- Raw data: `py_{before,after}_r{1,2,3}.md` and `_log.txt`. The full table
  and per-round values are in `analysis.txt`.

| Benchmark | CPython 3.14t | protopy BEFORE | protopy AFTER | A/B | protopyc BEFORE | protopyc AFTER | A/B | 10-21 protopy / protopyc (context) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `startup_empty` | 25.5 | 23.8 | 23.6 | 0.990 | — | — | — | 21.3 / — |
| `int_sum_loop` | 531.3 | 485.4 | 488.5 | 1.006 | 316.8 | 326.7 | 1.031 | 463.8 / 305.4 |
| `list_append_loop` | 26.8 | 159.9 | 161.9 | 1.013 | 173.2 | 179.7 | 1.037 | 165.9 / 180.3 |
| `str_concat_loop` | 25.6 | 144.6 | 141.9 | 0.981 | 198.0 | 197.6 | 0.998 | 145.7 / 196.9 |
| `range_iterate` | 328.1 | 531.2 | 508.5 | 0.957 | 341.0 | 337.8 | 0.991 | 554.3 / 377.4 |
| `multithread_cpu` | 208.9 | 126.8 | 124.2 | 0.980 | 51.7 | 49.5 | 0.959 | 118.1 / 67.8 |
| `attr_lookup` | 382.9 | 1633.9 | 1598.7 | 0.978 | 880.8 | 898.1 | 1.020 | 1577.8 / 867.7 |
| `call_recursion` | 36.7 | 208.1 | 203.7 | 0.979 | 174.2 | 174.4 | 1.001 | 205.0 / 171.3 |
| `pyperf_fib` | 95.7 | 450.3 | 434.6 | 0.965 | — | — | — | 422.5 / — |
| `pyperf_binary_trees` | 37.7 | 261.3 | 258.9 | 0.991 | — | — | — | 252.7 / — |
| `pyperf_nqueens` | 54.4 | 110.2 | 108.2 | 0.982 | — | — | — | 103.1 / — |
| `pyperf_richards_lite` | 26.9 | 40.4 | 41.0 | 1.014 | — | — | — | 38.4 / — |
| `pyperf_sieve` | 29.0 | 101.9 | 101.7 | 0.998 | — | — | — | 97.2 / — |
| `memory_pressure` (informational) | 56.2 | 1635.7 | 1619.8 | 0.990 | 1629.2 | 1645.6 | 1.010 | 1564.9 / 1650.6 |

Geomeans, with `memory_pressure` excluded:

| | protopy (13) | protopyc (7) |
|---|---:|---:|
| AFTER / BEFORE | **0.987** | **1.005** |
| AFTER / CPython 3.14t | 2.55x | 1.88x |
| BEFORE / CPython 3.14t | 2.60x | 1.90x |
| 10-21 final (context) | 2.52x | 1.91x |

- The port is neutral on the normal path.
- protopy is 1.3 % faster. Every per-benchmark ratio lies within the
  per-round spread; for example, `multithread_cpu` varies by ±20 % between
  rounds.
- protopyc's `int_sum_loop` (+3.1 %) and `list_append_loop` (+3.7 %) are the
  largest slowdowns. In both, the BEFORE and AFTER per-round values overlap:
  `int_sum_loop` BEFORE ran 313.8-317.1 ms and AFTER 312.4-327.7 ms.
  **Not isolated; probably noise.**
- The `pyperf_*` protopyc cells are "—" on purpose. `run_module` prints
  `Done.` last, so the harness's output-pattern check drops these runs. The
  10-21 reports have the same gap.
- `verify.txt` shows that these compiled modules do compute the right
  results.

## protopyc compile time (`compile.txt`)

- Each figure is the wall time of `protopyc X.py --build-so`, from source to
  a loadable `module.so`: the median of 3 repetitions, BEFORE and AFTER
  interleaved.

| Source | BEFORE ms | AFTER ms | A/B | generated C++ lines B → A | `module.so` bytes B → A |
|---|---:|---:|---:|---:|---:|
| `int_sum_loop` | 1212 | 1232 | 1.016 | 503 → 523 | 40,672 → 40,672 |
| `attr_lookup` | 1568 | 1564 | 0.998 | 634 → 654 | 58,896 → 58,896 |
| `call_recursion` | 1388 | 1388 | 0.999 | 594 → 614 | 46,800 → 46,800 |
| `bench_richards_lite` | 2104 | 2154 | 1.024 | 1037 → 1057 | 98,872 → 98,872 |
| `raise_across5` (probe) | 1294 | 1216 | 0.940 | 576 → 598 | 48,304 → 42,416 |
| `keyerror` (probe) | 1136 | 1154 | 1.015 | 509 → 531 | 42,424 → 36,952 |

- Compile time is unchanged, within ±6 %. The C++ compiler dominates it.
- The emitter now writes 20-22 more lines per module.
- Modules with `try`/`except` came out 12-13 % *smaller*.

## Exception probes ([`probes/`](probes/), `probes.txt`)

- Method: `perf stat -r 3` cycles, all five variants interleaved per probe.
- Each figure is cycles per iteration net of start-up. Start-up is measured
  by `startup.py`, a one-line script, and subtracted before dividing by the
  iteration count.
- protopyc runs `run_module module.so`. Every probe's `CHECK` line was
  checked against CPython's.

| Probe | iters | protopy BEFORE | protopy AFTER | A/B | protopyc BEFORE | protopyc AFTER | A/B | CPython 3.14t |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `empty_loop` | 2M | 77 | 75 | 0.98x | 45 | 39 | 0.87x | 126 |
| `try_no_raise` | 2M | 134 | 129 | 0.96x | 52 | 49 | 0.94x | 132 |
| `raise_catch` (`raise ValueError("x")`, same frame) | 200k | 82,247 | 82,630 | 1.00x | 21,295 | 21,990 | 1.03x | 644 |
| `raise_across5` (raised 5 frames down) | 100k | 637,069 | 658,390 | 1.03x | 67,423 | 67,083 | 0.99x | 3,078 |
| `zerodiv` (`1 // 0` caught) | 200k | 77,142 | 76,639 | 0.99x | 29,888 | 30,295 | 1.01x | 788 |
| `keyerror` (`d[k]` caught) | 200k | 93,170 | 92,558 | 0.99x | 36,732 | 37,682 | 1.03x | 664 |
| `foreign_math` (`math.sqrt(-1.0)` caught) | 200k | 95,823 | 95,058 | 0.99x | 55,001 | 57,556 | 1.05x | 1,352 |
| `hasattr_miss` | 200k | 11,089 | 11,387 | 1.03x | 16,164 | 15,539 | 0.96x | 287 |
| `gen_exhaust` | 200k | 260,723 | 265,032 | 1.02x | 212,230 | 209,173 | 0.99x | 518 |

- **The port does not change exception cost.** Every ratio is 0.87-1.05x.
  The figures are single `-r 3` readings, so ±5 % is noise.
- The pending-flag baseline `df185d1a` (10-21, whole-process cycles per
  iteration, not net of start-up) gives context:
  - `raise_catch` was 192,657 cycles and `keyerror` 205,103. The C++-exception
    design, kept by this port, still halves those.
  - A `try` that does not raise was 238 cycles, against about 130 now.
- A raise is still very expensive in absolute terms:
  - 128x CPython in protopy and 33x in protopyc for `raise_catch`.
  - Across five frames protopy pays about 640k cycles, about 110k per extra
    frame, against 67k in protopyc.
  - That is the C++ unwinder walking interpreter frames, plus building the
    exception object. It is unchanged by this port and is the obvious next
    target.

## Files and worktrees

- `build.sh`, `py_run.sh`, `py_verify.py`, `py_analyze.py`: the scripts.
- `analysis.txt`: the full table with per-round values.
- The worktrees are kept for the next measurement:
  - `/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022/before/{protoCore,protoPython}`
  - `/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022/after/{protoCore,protoPython}`
  - protoCore's standalone builds are in `protoCore/build_release/`.
  - protoPython's builds are in `protoPython/build_release/`, with its own
    copy of protoCore in `build_release/protoCore/`.
