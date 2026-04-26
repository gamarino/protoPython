# 2026-04-26 — pyperf subset baseline + minimal-getAttribute experiment

> **Status:** HEAD `377b615c` (V154 chain complete: Tier 1.1 + Tier 2 + Tier 2.5 + Tier 3 round 1 + ProtoList-allocation removal in MRO walks). Build Release. Tests: **149/149 pass**.

## TL;DR

- Honest pure-Python pyperformance subset on protopy at HEAD: geomean **~1.4×10³ ×** slower than CPython 3.14.
- Dominant cost is `richards_lite` (~7000–9000× CPython): class instantiation + method dispatch.
- `nqueens(8)` and `sieve(10000)` sit ~500–800× CPython: list subscript + tight integer ops.
- An experimental rewrite of `PythonEnvironment::getAttribute` (592 → ~100 lines, "trust protoCore's parent chain, synthesise Python views on demand") confirmed the hypothesis on simple cases but broke 137/149 regression tests. **Reverted.** Real refactor needs multi-iteration roadmap, not big-bang.

## pyperf subset numbers

Three independent runs of `benchmarks/pyperf/run_pyperf_subset.py` against `build-release/src/runtime/protopy`. Each script does internal warmup and reports `min=` of 5 timed iterations (no fork/exec in the measurement).

| Run | nqueens(8) | sieve(10000) | richards_lite | Geomean ratio |
|-----|------------|--------------|---------------|---------------|
| 1   | 585×       | 540×         | 8942×         | 1413×         |
| 2   | —          | 840×         | 7189×         | 1562×         |
| 3   | —          | 536×         | 6967×         | 1234×         |

Range 1234–1562×, median ≈ **1413×**. Variance comes mostly from richards_lite (GC pause scheduling and thread initialisation in our implementation; CPython's reference is ~0.2 ms which makes the ratio amplify any jitter on our side).

The previous report (V95 microbenchmarks, geomean 23.64×) understated the real gap: tight `s += i` loops mostly exercise the SmallInt fast path. The pyperf subset stresses what real Python code does — list subscript, method dispatch, class instantiation — and that is where protoPython pays.

## The minimal-getAttribute experiment

### Hypothesis

`PythonEnvironment::getAttribute` is 592 lines. It walks both protoCore's parent chain *and* Python's `__mro__`, has `__keys__` fallbacks, recursive descriptor pre-walks, and several bespoke special-case branches. protoCore already implements a chain walk with an inline cache. protoJS uses *only* QuickJS's compiler (not the QuickJS runtime) and calls `protoCore->getAttribute` directly — yielding orders-of-magnitude better numbers on equivalent workloads. **Hypothesis:** trust protoCore's chain walk; synthesise Python-only views (`__class__`, `__dict__`, `__mro__`) on demand from the chain; let descriptor protocol fire in a narrowly-scoped fast path.

### What was built

A ~100-line replacement that:
- Defers to `obj->getAttribute(ctx, name)` for the chain walk.
- Synthesises `__class__` via `getType` (calls `getFirstParent` which resolves mutable snapshots).
- Binds methods only on instance access (not on class access — fixed a regression where `c is type` returned True).
- Filters descriptor protocol: only fires when `val->hasOwnAttribute("__get__") == PROTO_TRUE`, i.e. no recursive descriptor probes.
- Falls back to `__getattr__` only on real miss.

### What worked

Smoke tests passed correctly:
- `class C: …; c = C(); c is C → False, c is type → False` ✓
- Simple `__init__` + method call: `OK 18` ✓

### What broke

Full ctest suite (`-E "regression|cpython_regr"`): **8% pass / 137 fail / 149 total.**

The defenses in the old code encode real Python semantics that the regression suite verifies: descriptor protocols on data descriptors, `__getattribute__` precedence, slot resolution rules, metaclass attribute lookup, etc. Each one was likely added because a specific test failed without it. The minimal version is *closer to right* in spirit (protoCore's chain walk is the correct primitive), but it cannot replace 592 lines of accumulated semantic compliance in a single commit.

### Decision

Reverted. Source files clean at HEAD `377b615c`. Build cleaned and re-verified — 149/149 tests pass.

The hypothesis is confirmed in principle: protoCore's primitives are sufficient, and the redundant Python-side walks are a meaningful fraction of the per-attribute cost (the recent `377b615c` commit removed `ProtoList` allocations from two MRO walks and shaved 21–28% off pyperf scripts). But the path forward is not a rewrite — it is identifying each defense, the test that requires it, and replacing it with a leaner equivalent that still trusts protoCore. One test at a time.

## Useful side-effect: `getFirstParent`

The experiment produced one keeper: `ProtoObject::getFirstParent(context)` in protoCore (`headers/protoCore.h` + `core/ProtoObject.cpp`). Unlike `getPrototype()`, it resolves mutable snapshots before reading the parent link — so for Python classes that have been mutated (e.g. via `addParent` / `__bases__` reassignment), it returns the live parent rather than the pre-mutation parent. Used today only in `getType`'s fallback path, but useful any time a caller needs the first parent of a possibly-mutable object.

## Roadmap (not a commitment)

1. **Per-defense audit of `getAttribute`.** For each branch in the current 592-line function, find the test that requires it. If no test, the branch is dead — remove. If a test requires it, document *what* semantic it encodes.
2. **Replace defenses incrementally.** Pick the most expensive branch (likely the `__keys__` fallback or the explicit MRO walk). Replace with a protoCore-backed equivalent. Run full suite; if green, commit; if red, identify the gap and add a narrowly-scoped guard.
3. **Re-bench at each step.** Don't trust microbenchmarks; run pyperf subset.

The end state should look like protoJS's call site — a small dispatch in front of `protoCore->getAttribute`, with Python-specific synthesis (descriptors, `__getattr__`) layered on the *result*, not woven through the walk.

## Files of record

- `protoPython/src/library/PythonEnvironment.cpp` — reverted to HEAD; the 592-line `getAttribute` is the current reality.
- `protoCore/headers/protoCore.h` + `protoCore/core/ProtoObject.cpp` — `getFirstParent` retained.
- `benchmarks/pyperf/run_pyperf_subset.py` — the harness used for the numbers above.
