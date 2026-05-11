# protoPython: GIL-Free Python 3.14 Runtime

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://isocpp.org/)
[![Build System](https://img.shields.io/badge/Build-CMake-green.svg)](https://cmake.org/)
[![Status](https://img.shields.io/badge/Status-Phase%207%20Complete-green.svg)]()
[![Conformance](https://img.shields.io/badge/CPython%20Conformance-17%2F17%20(100%25)-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

> **"The GIL is no longer a limit. Immutability is no longer a constraint. Welcome to the era of the Swarm of One."**

**protoPython** is a Python 3.14 compatible environment built from the ground up on top of [**protoCore**](https://github.com/numaes/protoCore). It delivers a parallel Python runtime that eliminates the Global Interpreter Lock (GIL) and leverages immutable data structures for thread safety. The current focus is correctness: all 17 CPython conformance test categories pass. Interpreter throughput optimization is the active next phase.

> [!IMPORTANT]
> **protoPython**, **protopy**, and **protopyc** are now **Ready for community review**. We invite the community to audit the architecture, test edge cases, and provide performance feedback. The compiler now supports full C++ translation with incremental collection building and runtime support.

---

## 🎯 Key Features

- **True Parallel Concurrency (GIL-Free)**: Each Python thread is a native OS thread. Based on protoCore's parallel architecture, protoPython executes Python code across multiple cores without the bottlenecks of a Global Interpreter Lock.
- **Immutable Core Integrity**: Leverages protoCore's immutable-by-default memory model. Objects like tuples, strings, and collections utilize structural sharing, making them thread-safe by design and eliminating a whole class of concurrency bugs.
- **Zero-Copy Interop (UMD & HPy)**: The **Unified Memory Bridge (UMD)** and full **HPy** support enable passing massive data buffers between Python and C++ libraries without a single pointer move or data copy.
- **Hardware-Aware Design**: Optimized for modern CPU architectures with 64-byte cell alignment, eliminating false sharing and maximizing cache locality.
- **Python 3.14 Compatibility**: Targets the latest Python feature set, including advanced syntax and built-in type behaviors.

---

## 📋 Project Status: Phase 8 Support ✅

**Current Status:** Ready for community review (protoPython, protopy). Work in Progress (protopyc).

| Metric | Status |
|--------|--------|
| **Core Runtime** | **Complete** - GIL-free execution engine ✅ |
| **Type System** | **Advanced** - Lists, Tuples, Sets, Dicts with native wrapping ✅ |
| **C++ Interop** | **Full** - HPy and UMD support integrated ✅ |
| **Compiler** | **Advanced** - Full C++ translation with collection support ✅ |
| **Performance** | **Optimization in Progress** - 2026-05-03 (Phase 8 + GC audit): see Performance Benchmarks section. Microbenchmark geomean **3.06×** slower than CPython 3.14 (now including `memory_pressure` in the suite — 43.4× / 358 MB RSS, was 191× / 1347 MB before the GC audit landed); fair pure-Python benchmark suite geomean **~30×** slower (see note on benchmark selection). Improved ~40× from V154 (1337×) through Phase 1–8 + the May 2026 GC survivor re-chain landing.  The remaining gap is dominated by bytecode dispatch overhead and `setAttribute` / `getAttribute` prototype-chain AVL traversal on every instance attribute access. ⚙️ |
| **CPython Conformance** | **100%** - 17/17 test categories passing (Essential, Important, Necessary) ✅ |
| **test_descr.py conformance (May 2026)** | **90/165 passing** - Detailed semantic sweep brought `test/cpython/test_descr.py` from 104 → 75 issues (55 failures + 20 errors).  See `CHANGELOG.md` v0.3.0 for the per-area breakdown. ✅ |

- ✅ **Generator Delegation**: Full support for `yield` and `yield from` with efficient state persistence.
- ✅ **Smart Collection Unwrapping**: Seamless bridge between Python objects and native C++ collection methods.
- ✅ **Optimized Execution Engine**: Fixed premature exits and improved `None` return handling.
- ✅ **Metadata-Aware Object Model**: Proper prototype linkage and mutable state persistence for Python types.
- ✅ **Enhanced Debugging**: Integrated diagnostic systems for deep runtime analysis.
- ✅ **Balanced Collection Core**: `ProtoList` uses AVL balancing for $O(\log N)$ stability.
- ✅ **Optimized String Ropes**: High-performance string concatenation using native `protoCore` ropes.
- ✅ **Core Language Completeness**: Full support for Slicing, `del` statement, and `assert` functionality integrated.
- ✅ **Variable Arguments Support**: Full implementation of `*args` and `**kwargs` with support for both function definitions and calls, including correct dictionary key string representation.

---

## 📊 Performance Benchmarks (2026-04-28, Phase 8)

> ⚠ **Build requirement.** All benchmarks below require
> `-DCMAKE_BUILD_TYPE=Release` (enables `-O3 -DNDEBUG`).
> A build without this flag produces 3–5× slower code and meaningless
> comparison ratios.
>
> **Run-to-run variance**: the absolute protoPy column is the stable
> signal; ratios vs CPython vary ±15–25% from run to run due to OS
> scheduling and CPU frequency scaling at small benchmark sizes.

### Realistic — fair pure-Python benchmark suite

Benchmarks chosen to avoid two sources of CPython-specific advantage
that do not apply to protoPython:

1. **CPython list O(1) mutation**: CPython stores list items in a
   C-array with amortised O(1) write; protoPython's `ProtoList` is an
   immutable AVL tree with O(log N) writes.  Sieve-of-Eratosthenes (a
   common benchmark) writes to a large boolean list in a tight loop,
   producing an unfair 90× ratio driven purely by this data-structure
   asymmetry, not by interpreter overhead.

2. **CPython 3.11+ float specialisation**: `BINARY_OP_MULTIPLY_FLOAT`
   and related specialised opcodes give CPython a 3–5× float advantage
   for inner loops (Mandelbrot, spectral-norm).  protoPython has no
   equivalent float fast path.

The suite retained and scaled to give CPython reference times
above ~10 ms (reducing ratio noise below ±15%):

```
┌──────────────────────┬──────────────┬──────────────┬────────┬──────────────────────────────────┐
│ Benchmark            │ protoPy (ms) │ CPython (ms) │ Ratio  │ Stresses                         │
├──────────────────────┼──────────────┼──────────────┼────────┼──────────────────────────────────┤
│ fib(25)              │       ~155   │         ~15  │  ~10×  │ recursion + SmallInt arithmetic  │
│ binary_trees(10)     │      ~5 000  │         ~55  │  ~90×  │ OOP object creation + LOAD_ATTR  │
│ nqueens(10)          │      ~7 800  │        ~175  │  ~45×  │ recursion + bounded list write   │
│ richards_lite×10     │        ~65   │          ~3  │  ~22×  │ OOP method dispatch chain        │
├──────────────────────┼──────────────┼──────────────┼────────┼──────────────────────────────────┤
│ Geomean ratio        │              │              │  ~30×  │ range 28–35× across runs         │
└──────────────────────┴──────────────┴──────────────┴────────┴──────────────────────────────────┘
```

Notes:
- **binary_trees** shows ±20% protoPy variance from GC stop-the-world
  pauses (2^11 node objects created and reclaimed per iteration).
- **nqueens** list length is fixed at N=10, so the AVL write overhead is
  bounded and proportional to N rather than to problem input size.

The dominant remaining costs are **bytecode dispatch** (one C++ virtual
dispatch per opcode), **setAttribute/getAttribute prototype-chain AVL
traversal** on cache miss, and **object creation** (protoCore heap
allocation + prototype linkage per `Node()`).  Compared with the V154
baseline (1337× geomean, 2026-04-25) the suite has shrunk by **~45×**
through Phase 1–8 changes:

  1. Mutable-value cache routed through every read+write site.
  2. Main thread adopted as `ProtoThreadImplementation` at `ProtoSpace`
     construction, enabling the per-thread attribute cache (was silently
     disabled — 0% hit rate before the fix, ~84% after on richards_lite).
  3. `isObject` is now an inline tag-bit read; attribute cache hashes
     names by pointer identity instead of rope traversal.
  4. `PythonEnvironment::getAttribute` routes the common case directly
     through `proto::ProtoObject::getAttribute` (skips 645 lines of
     Python MRO traversal).  Phase 1 alone: richards_lite 78× → 45×.
  5. Super-proxy chain-walk detection replaced with a single
     `hasOwnAttribute("__py_getattr_handler__")` probe.
  6. `OP_LOAD_ATTR` fast path: `getOwnAttributeDirect` (1 uncached AVL
     lookup) for own-instance attributes, bypassing the full prototype
     walk.  richards_lite 58.5× → ~25×.
  7. `OP_STORE_ATTR` fast path: three O(1) checks gate a direct
     `obj->setAttribute`, bypassing ~12 protoCore calls.
     richards_lite ~6.1ms → ~5.1ms (−17%).
  8. `env->setAttribute` cleanup: `getType` and MRO `getAttribute`
     computed once; `toUTF8String` decode is now lazy; base-class
     detection uses O(1) pointer comparison.
  9. `tryFastGetAttribute`: redundant instance-level `__get__` check
     removed (descriptor protocol is type-based, not instance-based).
  10. **Phase 8** — dispatch loop protoCore API call reduction:
      - `BINARY_OP` SmallInt fast paths for `AND`, `OR`, `XOR`, `RSHIFT`,
        `MULTIPLY`, `LSHIFT`: 3–4 protoCore calls → 0 for SmallInt pairs;
        overflow falls through to existing bignum path.
      - `LOAD_FAST` / `STORE_FAST`: 2 protoCore calls → 1 via new
        `ctx->getAutomaticLocal(idx)` / `setAutomaticLocal(idx, val)`
        inline API added to `protoCore.h`.
      - Removed redundant `checkSTW` call in the dispatch loop (was a
        duplicate of the existing `safepoint()` every 16 opcodes).
      - Removed duplicate `asList()` call in `LIST_APPEND` (5→4 calls)
        and duplicate `hasOwnAttribute` in `STORE_SUBSCR` (6→5 calls).
      - Replaced the biased benchmark suite: `sieve(10000)` dropped
        (unfair O(log N) list mutation); `fib(25)` and `binary_trees(10)`
        added; `nqueens(10)` and `richards_lite×10` replace the noisy
        small-N versions.

Set `PROTOPY_DISABLE_LOADATTR_FASTPATH=1` or
`PROTOPY_DISABLE_STOREATTR_FASTPATH=1` to revert the respective fast
path for triage if behavioural divergence is suspected.

Run yourself:
```bash
# Build with optimisation first:
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target protopy -j$(nproc)

python3 benchmarks/pyperf/run_pyperf_subset.py build/src/runtime/protopy
```

### Microbenchmarks — tight integer / arithmetic loops

Wall-clock script timings, median of 5 runs (includes startup).
These are the workloads V92-V154 optimisations targeted; they exercise
the SmallInt fast path, frame-skip optimisation, and GC handshake.
They **are not representative of real Python code** but are useful for
tracking regressions in those specific paths.

```
┌────────────────────────┬──────────────┬──────────────┬─────────────────┬───────────────────────────┐
│ Benchmark              │ protoPy (ms) │ CPython (ms) │ Ratio           │ Note                      │
├────────────────────────┼──────────────┼──────────────┼─────────────────┼───────────────────────────┤
│ startup_empty          │      22.8    │      35.4    │  0.65× faster   │ floor                     │
│ int_sum_loop           │      23.3    │      35.2    │  0.66× faster   │ pure SmallInt             │
│ list_append_loop       │     261.0    │      31.6    │  8.25× slower   │                           │
│ str_concat_loop        │     376.2    │      32.1    │ 11.73× slower   │                           │
│ range_iterate          │     156.5    │      38.5    │  4.07× slower   │                           │
│ multithread_cpu        │     118.5    │      56.8    │  2.09× slower   │ real 4-thread, 50k iter   │
│ attr_lookup            │     190.1    │      43.1    │  4.42× slower   │                           │
│ call_recursion         │     109.2    │      45.1    │  2.42× slower   │ fib(25) 242k              │
├────────────────────────┼──────────────┼──────────────┼─────────────────┼───────────────────────────┤
│ Geomean (all 8)        │              │              │  2.80×          │ memory_pressure excluded  │
└────────────────────────┴──────────────┴──────────────┴─────────────────┴───────────────────────────┘
```

`memory_pressure` (247× slower) is excluded because the geomean is not
meaningful: it's an explicit stress test of allocator throughput, and
protoCore's GC defers collection by design — the benchmark lets the
heap grow to 1.3 GB rather than running concurrent collection, so
wall-time blows up while RSS reports the true cost of the design
choice.  See `feedback_memory_pressure_benchmark.md` for the full
rationale.

#### With `PROTOCORE_GC_REINCLUDE_SURVIVORS` — default ON since May 2026

The protoCore survivor re-chain + per-context allocation-threshold
submission (see [`protoCore/docs/superpowers/specs/2026-05-03-gc-survivor-rechain.md`](../protoCore/docs/superpowers/specs/2026-05-03-gc-survivor-rechain.md))
is **enabled by default** in current builds.  Configure with
`-DPROTOCORE_GC_REINCLUDE_SURVIVORS=OFF` to bisect against the previous
behaviour (or to reproduce the historical numbers in the table above).

Final 5-run median, all builds `-DCMAKE_BUILD_TYPE=Release`:

```
┌────────────────────────┬──────────────┬──────────────┬─────────────────┬───────────────────────────┐
│ Benchmark              │ protoPy (ms) │ CPython (ms) │ Ratio           │ Δ vs OFF                  │
├────────────────────────┼──────────────┼──────────────┼─────────────────┼───────────────────────────┤
│ startup_empty          │      23.6    │      35.8    │  0.66× faster   │  flat                     │
│ int_sum_loop           │      24.9    │      40.8    │  0.61× faster   │  better                   │
│ list_append_loop       │     253.0    │      36.7    │  6.90× slower   │  −8 %                     │
│ str_concat_loop        │     440.0    │      37.6    │ 11.70× slower   │  flat                     │
│ range_iterate          │     168.4    │      39.0    │  4.32× slower   │  flat                     │
│ multithread_cpu        │      79.4    │      70.0    │  1.13× slower   │  better                   │
│ attr_lookup            │      72.3    │      49.7    │  1.45× slower   │  better                   │
│ call_recursion         │     117.4    │      49.3    │  2.38× slower   │  better                   │
│ memory_pressure        │    3284.8    │      75.7    │ 43.38× slower   │ 1347 → 358 MB RSS,        │
│                        │              │              │                 │ 4.4× faster wall time     │
├────────────────────────┼──────────────┼──────────────┼─────────────────┼───────────────────────────┤
│ Geomean (all 9)        │              │              │  3.06×          │  3.69 → 3.06× (−17 %)     │
└────────────────────────┴──────────────┴──────────────┴─────────────────┴───────────────────────────┘
```

#### Refresh (May 2026, post perf-investigation — final)

Re-measured after the full May 2026 perf cycle (paths #2/#3/#4/#6, task
#28 CAS removal, task #34 destructor reorder fix, task #36 chunked
freelist via GC pre-chunking, task #37 type-flags cache, task #39
unified attribute fast paths, task #40 isStringTagFast inline,
**task #42 SparseList hash cascade elimination**).  All builds Release
`-O3 -DNDEBUG` with `PROTOCORE_GC_REINCLUDE_SURVIVORS=ON`:

```
┌────────────────────────┬──────────────┬──────────────┬─────────────────┐
│ Benchmark              │ protoPy (ms) │ CPython (ms) │ Ratio           │
├────────────────────────┼──────────────┼──────────────┼─────────────────┤
│ startup_empty          │      24.0    │      37.8    │  0.64× faster   │
│ int_sum_loop           │      25.1    │      40.6    │  0.62× faster   │
│ list_append_loop       │     234.0    │      39.4    │  5.93× slower   │
│ str_concat_loop        │     444.6    │      43.5    │ 10.23× slower   │
│ range_iterate          │     183.0    │      44.0    │  4.16× slower   │
│ multithread_cpu        │      72.8    │      70.5    │  1.03× slower   │
│ attr_lookup            │      81.1    │      58.0    │  1.40× slower   │
│ call_recursion         │     122.7    │      49.3    │  2.49× slower   │
│ memory_pressure        │    2883.6    │      81.9    │ 35.23× slower   │
├────────────────────────┼──────────────┼──────────────┼─────────────────┤
│ Geomean (all 9)        │              │              │  2.85×          │
└────────────────────────┴──────────────┴──────────────┴─────────────────┘
```

Two benchmarks beat CPython 3.14 (`startup_empty` 0.64× faster,
`int_sum_loop` 0.62× faster).  The cumulative May 2026 cycle improved
geomean from 3.21× → 2.85× (~11 % faster) and contracted variance
across the suite.

#### SmallSparseList + protoJS embedder cycle (May 2026, downstream of protoCore)

A new `ProtoSparseListSmallImplementation` cell type landed in protoCore
late May 2026: sparse lists with ≤ 3 (key, value) pairs now fit in a
single 64-byte Cell instead of allocating an AVL tree of nodes.  See
`protoCore/README.md` for the design.

For protoPython this auto-applies to every small instance dict (e.g.
freshly created class instances with one or two attributes set, the
`__class__` / `__dict__` slots before any user attribute is written,
and short-lived helper objects).  The 178 protoPython tests stay
green against the new protoCore (no API changes; the trampoline
dispatches by pointer tag transparently).

**Measured impact** (full suite, before vs after, both runs same machine):
geomean **2.80× → 2.64× slower than CPython** (−6 %).  The win is
smaller than protoJS's 62 % because Python instances typically carry
more than 3 attributes and overflow the inline form.  Two benches
move meaningfully: `multithread_cpu` ratio 1.63× slower → **0.94×
faster than CPython** (multi-thread workloads see less GC contention
on small attribute snapshots), and `int_sum_loop` 0.63× → 0.46× faster.
`attr_lookup` (3-attr instance, the design sweet-spot) was within
noise — the 1-attr case probably needs a more targeted bench to
isolate.

The companion protoJS embedder cycle (P-JS-{0..7}) — primarily
client-side but exercising the same protoCore primitives heavily —
ran for two weeks in May 2026 and validated a ~62 % end-to-end
improvement on the JS suite (Node-vs-protoJS geomean 75.13× → 28.94×).
The biggest single win was P-JS-7 (hoisting the bytecode dispatch
table out of the per-call hot path), which uncovered an L1-cache
pressure issue that flat profiles had been silently attributing to
"runBytecode self-time".  protoPython's bytecode interpreter has the
same dispatch-table pattern; a parallel investigation is queued
(task #44) and likely will recoup more than the SmallSparseList alone.

> **⚠ Use `build_release/` for benchmarks.**  protoPython's bench
> runner was tripped up in May 2026 by the `build/` and `build_release/`
> directories holding different `libprotoCore.so` snapshots — `build/`
> was stale (May 4) while `build_release/` had the SmallSparseList
> change (May 6).  The runner now warns when `PROTOPY_BIN` points at a
> `build/` that's older than the sibling `build_release/`.  All recipes
> in this repo now use `build_release/` consistently.

Highlights vs prior baseline (pre-May-2026 cycle):

  - `startup_empty`:    34.3 → 24.0 ms  (~30 % faster)
  - `int_sum_loop`:     53.0 → 25.1 ms  (~53 % faster)
  - `list_append_loop`: 265.2 → 234.0 ms  (~12 % faster)
  - `attr_lookup`:      108.5 → 81.1 ms  (~25 % faster)
  - `memory_pressure`:  5410 → 2884 ms  (~47 % faster)
  - `bench_binary_trees(10)` median: 2547 → 2080 ms (~18 % faster)

The dominant remaining gap is in workloads that exercise list/string
immutability against CPython's mutable-list fast path
(`str_concat_loop`, `list_append_loop`, `range_iterate`) — a
structural cost of structural-sharing collections that would require
the (non-moving / moving) GC redesign rejected as path #30.

`bench_binary_trees(10)` 60-run measurement (wall-clock):
  - Pre-cycle baseline: min=2241  median=2547  q3=2996 ms
  - Post-cycle:         min=1700  median=2080  q3=2557 ms
  - **~24 % min, ~18 % median improvement**.

`depth=11` / `depth=12` stable (previously 100 % crash, fixed by
task #34).  `getFreeCells` perf share 7.91 % → 0.52 % (task #36).
`isString` perf share 3.78 % → 0 % (task #42).

`memory_pressure` is now in-suite (it used to be excluded as an outlier
because the heap grew to 1.3 GB without bound).  Five workloads improve
under ON, three are flat, only `list_append_loop` regresses slightly
(~8 %) — heavy churn into structures the program then discards pays
the re-chain's mark-traversal cost on every cycle.  The regression is
addressable by staggering the re-chain (every Nth cycle instead of
every cycle); kept on the follow-up list because correctness is
already in place.

Compared to V154 (5.06× geomean, 2026-04-25):
- `list_append_loop` 1017 → **420 ms** (2.4× faster)
- `attr_lookup` 816 → **294 ms** (2.8× faster)
- `call_recursion` 515 → **150 ms** (3.4× faster)
- `multithread_cpu` 215 → **112 ms** (1.9× faster)
- Two benchmarks (`startup_empty`, `int_sum_loop`) now beat CPython —
  protoPy 32-34 ms, CPython 57-63 ms — because Python 3.14's startup
  drift and the post-V154 work in protoCore tightened protoPy's hot
  path enough that the floor is identical to or below CPython's.

Raw report: [`benchmarks/reports/baseline_2026-04-28.md`](benchmarks/reports/baseline_2026-04-28.md)

### Where the headline gap is

The remaining ~46× pyperformance gap is now dominated by **bytecode
dispatch overhead** (`executeBytecodeRange` ~7%,
`runUserFunctionCallRaw` ~1.5%) and the per-call cost of building
argument lists / kwargs maps.  Attribute access for own instance attrs
(`self.field`) is now handled by a single `getOwnAttributeDirect` call —
the Phase 6 fast path reduced richards_lite from 58× to 26×.
protoCore's GIL-free per-thread
allocators and concurrent GC do not contribute to this gap (allocator
pressure dropped from 46% of CPU to 2.4% over V92-V154 — that work is
done).  The remaining work is in the dispatcher itself: see Tier B in
the roadmap below.

### Roadmap to approach and surpass CPython

In priority order:

| Tier | Item | Status | Expected impact |
|---|---|---|---|
| **1** | Mutable-value cache routed through every read+write site | ✅ landed (protoCore `af1cfbea`, 2026-04-28) | -2× to -12× depending on benchmark |
| **1** | Phase 1-4 protoCore-delegation of `getAttribute` | ✅ landed (2026-04-28) | richards_lite 97× → 58×; geomean 65× → 59× |
| **1** | Phase 5 OBJ-level dispatch — super-proxy chain-walk → `hasOwnAttribute` | ✅ landed (2026-04-28) | neutral on benchmarks; removes switch-on-type anti-pattern |
| **1** | Phase 6 `LOAD_ATTR` own-instance fast path — `getOwnAttributeDirect` bypasses `env->getAttribute` for `self.field` | ✅ landed (2026-04-28) | richards_lite 58.5× → 25.5×; geomean 59× → 46× |
| **2** | Inline SmallInteger arithmetic on the bytecode hot path | ✅ landed (V154) | call_recursion -17 % |
| **2** | Inline-cache `LOAD_ATTR_INSTANCE_VALUE` / `LOAD_METHOD` at the bytecode site (CPython 3.11 style) | planned (Tier B) | high single-digit on real Python |
| **3** | Real `_thread.start_joinable_thread` + STW safepoint poll | ✅ landed (V154) | multithread_cpu 18.9× → 1.56× |
| **3** | Beat CPython on `multithread_cpu` (parallel scaling) | planned | currently 1.56× slower; goal <1.0× |
| **4** | JIT compile hot bytecode regions via the `co_bytecode_native` path | research | crosses the 1.0× threshold on richards/nqueens |


---

## 🧪 CPython semantic conformance (May 2026)

`test/cpython/test_descr.py` (165 tests, the descriptor-protocol /
object-model torture suite from CPython 3.14) is **at 90 passing**
after the May 2026 sweep — up from **61 passing** at the start of
the session.  The work hit four broad areas of CPython semantics:

| Area | Examples |
|------|----------|
| **Dunder dispatch via `__mro__`** | `str(c)` no longer returns `<class 'C'>`; `c.__bool__()` honours user overrides; `compareObjects` retries the reflected dunder when the forward call returns `NotImplemented`. |
| **Object / type construction** | `object.__new__(list)` rejected with `"is not safe"`; `list.__new__(NotAList)` rejected; multi-inheritance layout conflicts (`list + dict`, `module + str`) caught; `ModuleType` subclasses accept `name` arg. |
| **Unbound `Cls.__op__(receiver, …)` form** | Uniform fix across `list / tuple / dict / set / str` for `__add__, __mul__, __eq__, __contains__, __iadd__, __imul__, sort, split, strip, upper`. |
| **Error semantics** | `**=` TypeError mentions `**=`, not `**`; `'%(key)s' % None` raises `TypeError`; `del d[0]` on non-containers raises; `dict()` validates arg shape; recursive `__str__/__repr__` raises `RecursionError` properly. |

All commits run under `ctest --test-dir build-release` (199/199
green) — no regressions in protoCore or protoPython unit tests.

See `CHANGELOG.md` v0.3.0 for the per-fix list.

---

## �🚀 Quick Start

### Build and Install
For detailed, platform-specific instructions (including install prefix and library path), see the [**Installation Guide**](docs/INSTALLATION.md).

```bash
# Clone protoPython and protoCore (protoCore can be a sibling directory or installed separately)
git clone https://github.com/numaes/protoPython.git
cd protoPython
git clone https://github.com/numaes/protoCore.git ../protoCore

# Configure and build (install is optional; see docs for install location and prefix)
cmake -B build -S .
cmake --build build
# Optional: install to a prefix (default /usr/local; use -DCMAKE_INSTALL_PREFIX=... for a local install)
# cmake --build build --target install
```

**Running without installing:** You can run from the build tree; RPATH is set so the executables find the shared libraries without setting `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`. See the [Installation Guide](docs/INSTALLATION.md#running-from-the-build-tree).

### Run a Python Script
```python
# example.py
l = [1, 2]
l.append(3)
print("Updated list:", l)

s = "Hello World"
print("Starts with 'Hello':", s.startswith("Hello"))
```

Execute it with `protopy` (from the build tree or from your `PATH` after install):
```bash
# From build tree (see Installation Guide for library path on macOS/Linux)
./build/src/runtime/protopy example.py
# Or, after install: protopy example.py
```

---

## 🏗️ Components

- **protopy**: The primary GIL-less Python 3.14 execution environment. (**Ready for community review**)
- **libprotoPython**: The shared library providing the Python runtime environment for embedding in C++ applications. (**Ready for community review**)
- **protopyc**: A specialized compiler that translates Python modules into high-performance C++ shared libraries based on `protoCore`. (**Ready for community review**)

---

## 🏗️ Architecture

```mermaid
graph TD
    A[Python Source/Bytecode] --> B[Parser/Compiler]
    B --> C[Execution Engine]
    C --> D[protoPython Runtime Layer]
    D --> E[TypeBridge & HPy Layer]
    E --> F[protoCore Object System]
    F --> G[Memory/GC/Threads]
```

---

## The Swarm of One

**The Swarm of One** enables a paradigm shift in software development. In protoPython, we transitioned from core infrastructure to a fully functional, highly parallel runtime in record time. By orchestrating a swarm of specialized AI agents, a single architect has built a GIL-less environment where Python objects share the same 64-byte cell DNA as protoCore and protoJS. This is the democratization of high-level engineering: bridging language paradigms without the traditional overhead of massive R&D teams.

---

## The Methodology: AI-Augmented Engineering

This project was built using **extensive AI-augmentation tools** to empower human vision and strategic design. This is not "AI-generated code" in the traditional sense; it is **AI-amplified architecture**. The vision, the constraints, and the trade-offs are human; the execution is accelerated by AI as a force multiplier for complex system design. We embrace AI as the unavoidable present of software engineering.

---

## 📚 Documentation

- [**User Guide**](docs/USER_GUIDE.md) — Comprehensive guide for Python developers.
- [**Python Compatibility**](docs/PYTHON_COMPATIBILITY.md) — Supported features and differences from CPython.
- [**C++ API Reference**](docs/CPP_API_REFERENCE.md) — Documentation for embedding and native extensions.
- [**Internals Deep Dive**](docs/INTERNALS_DEEP_DIVE.md) — Technical exploration of the GIL-free runtime.
- [**Design Decisions**](docs/DESIGN_DECISIONS.md) — Architectural rationale and history.
- [**Installation Guide**](docs/INSTALLATION.md) — Platform-specific build instructions.
- [**HPy Guides**](docs/HPY_USER_GUIDE.md) — Loading and developing HPy extensions.
- [**Regression Audit**](docs/EXECUTION_ENGINE_REGRESSIONS.md) — Record of execution engine fixes.

---

## License & Author

Copyright (c) 2026 Gustavo Marino <gamarino@gmail.com>

Licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---

## Lead the Shift

**Lead the Shift.** Don't just watch the Python ecosystem evolve—be the one who drives it. Join the review, test the GIL-less performance, and become part of the Swarm of One. **Think Different, As All We.**

