# protoPython — embedder conformance

The normative rule table is `protoCore/docs/EMBEDDER-CONFORMANCE.md`. This file
records protoPython's **first-run result**, the judgement answers a script cannot
give, and what the adaptor does and does not prove.

- Adaptor: `test/library/ConformanceHost.h`
- Wiring: `test/library/ConformanceTests.cpp`, `test/library/ConformanceIsolate.cpp`
- Static ratchet: `conformance-allow.txt` (**exits 1 on purpose — two real defects**)
- Run: `ctest --test-dir build_release -R 'conformance' < /dev/null`

## First run — 2026-09-25, protoCore 2.3.0

| Case | Rule | Result | Numbers |
|---|---|---|---|
| `gc.young_submitted` | 1 | **PASS** | grew in-use by 786,432 cells; 4 cycles reclaimed 851,157; residual 26,071 (3.3%) |
| `gc.transient_reclaimed` | 5 | **PASS**, with a caveat that matters | grew 1,640,412; residual **164,667 (10.0%)**; reclaimed 1,579,673 |
| `gc.host_stress` | 3 | **PASS** | 20/20 rounds under a 400,000-cell ceiling, 21 cycles, 9,760,531 cells reclaimed |
| `symbol.fast_path_key_hits` | 4 | **PASS** | a 31-byte key is readable through both `getAttribute` and `getOwnAttributeDirect` |
| `external.finalizer_runs` | 7 | **PASS** | kernel characterisation |
| `module.root_survives_cycle` | 9b | **PASS** | kernel characterisation; `moduleRootCount` 44 → 45 |
| `module.alias_rejected` | 9c | **NEEDSREVIEW** | kernel keeps provider+path+version distinct; protoPython's own keying unchecked |
| `external.bytes_accounted` | 7 | **NEEDSREVIEW** | 15 external wrappers, no byte total — see C7 |
| `thread.registered` | 11 | **NOTAPPLICABLE** | `forEachThreadKind` not implemented. **Rule 11 UNVERIFIED by this case** |
| `stw.quorum_completes` | 2 | **PASS** (isolated) | a cycle advanced twice with a Python thread up (`runningThreads`=2) |
| `join.parks` | 2b | **PASS** (isolated) | a cycle completed while `Thread.join()` was blocked, reclaiming 521,935 cells |
| **`heap.ceiling_progress`** | **8** | **FAIL** (isolated) | see below |

## The rule-5 result contradicts the prediction, and the number is the finding

**The P4 plan predicted protoPython would FAIL rule 5.** It passes. The reason
the prediction was wrong is worth recording, because the plan's reasoning was not
merely unlucky — it was incomplete.

The plan argued that `OP_BUILD_TUPLE`, `OP_LIST_TO_TUPLE` and the `*args` binding
each allocate a fresh `ProtoTuple` per execution, that every interned tuple node
is perennial, and therefore that a tuple-heavy workload's heap grows
monotonically. Two of those three premises are right. What the plan missed is
that a protoPython tuple is a **mutable wrapper object plus a `ProtoTuple`
payload**: the wrapper, the intermediate `ProtoList` and the N `appendLast`
rebuilds are ordinary collectable cells and dominate the per-tuple cost. Only the
payload is perennial.

The measurement separates them without an opinion, which is precisely §D5's
argument for a measurement over a grep: **10.0% of the heap a tuple-heavy
workload consumes never comes back** (164,667 cells of 1,640,412), against 3.3%
for the same workload using strings. That residual is consistent with perennial
tuple nodes and it is a real monotonic leak — but it is far below the 0.50
threshold, which is calibrated to catch a runtime whose sequences are *entirely*
perennial. So: **rule 5 passes, and the 10% figure is an informational finding
for P5**, not a rule failure. Reporting it as a failure would have been the
overclaim; reporting only "PASS" would have thrown away the number.

## The finding: rule 8

```
protoCore: heap hard limit 249152 cells reached; live set 196471 cells,
last cycle reclaimed 0 — out of memory
```

**Reproduction:**
`build_release/test/library/test_conformance_isolate --case=heap.ceiling_progress`
(exit 134). The case settles the space, measures protoPython's own live set
(≈49,152 cells), sets a hard ceiling 200,000 cells above it — which no
protoPython code does, because protoPython never calls `setHeapLimits` — and runs
a **bounded-backlog** producer/consumer workload: 101 rounds of 2,000 deque items,
each round produced by two threads and fully drained before the next.

**Severity: high. Mechanism not established.** What is established: with at most
2,000 items in flight, the live set still grew by ≈147,000 cells above the
settled baseline, and two consecutive cycles reclaimed nothing. Candidates, in
the order worth checking:

1. **Per-round `threading.Thread` and `deque` objects accumulating.** 202 threads
   and 101 deques over the run. `_thread._shutdown` joins every live thread and
   protoPython does not yet track daemon vs non-daemon, which suggests thread
   bookkeeping is retained; and each deque is an external pointer with a
   finalizer, so its C++ state is only freed when the wrapper is swept.
2. **Module-global retention.** `_conf_pc_result` and the nested `producer`
   closure live in `__main__` for the whole run.
3. The P2 topology — every thread parked in `waitForHeapHeadroom`.

**The calibration history matters, because it is what makes this number
trustworthy.** Two earlier versions of this measurement produced a
confident-looking rule-8 failure that meant nothing: the first used a ceiling of
`heapSize + 32768`, only 32,768 cells above what the interpreter already held;
the second bounded the ceiling properly but used a workload that enqueued all
200,000 items before draining any, so the live set was unboundedly live **by
construction** and would exceed any ceiling. Both were fixed — the case now
settles and measures first, and `Host::runProducerConsumer`'s contract now
*requires* a bounded backlog — and the abort survived both fixes. That is why it
is reported as a finding rather than as an artefact.

## The two defects the static check leaves uncovered

`conformance-allow.txt` **exits 1 on purpose.**

### 1. `src/library/PythonEnvironment.cpp:11426` — a dead `== nullptr`, with a user-visible consequence

```cpp
if (needle.empty() && !sub->isInteger(context)
    && sub->getAttribute(context, ..."__data__") == nullptr && ...)
    return context->fromInteger(-1);
```

`getAttribute` returns `PROTO_NONE` for an absent attribute and `nullptr` only for
invalid input (`protoCore/core/ProtoObject.cpp:773-781`). At this point `sub`,
`context` and the key are all non-null, so the comparison is **always false** and
the whole `&&` chain is unreachable.

**Consequence, measured rather than asserted:**

```
$ protopy -c 'class W: pass
print(b"abc".find(W()))'
0
```

CPython raises `TypeError`. protoPython reports a match at index 0. The guard's
evident intent — "an empty needle from a non-bytes-like object means -1" — never
runs, so the call falls through to `haystack.find("")`, which returns `start`.
**Fix:** compare against `PROTO_NONE`. `py_bytes_count` immediately below shares
the structure and should be checked with it.

### 2. `src/library/PythonEnvironment.cpp:8668` — an always-true condition

```cpp
if (fs->hasAttribute(context, env->getClassString())) {
```

`hasAttribute` returns a boolean **object**: `PROTO_TRUE` is `1217UL` and
`PROTO_FALSE` is `193UL`, both non-null. The condition is **always true**.
**Fix:** `== PROTO_TRUE`.

Two further sites the checker reported and that are **correct** —
`BuiltinsModule.cpp:7102` and `PythonEnvironment.cpp:18447` — compare against
`PROTO_TRUE` on the following physical line. They were false positives of the
checker's first draft, which read only one physical line; the checker now joins
logical lines and both are clean. Recording that here because a checker that
flags correct code is worse than no checker.

## What the adaptor does not prove

**`forEachThreadKind` is deliberately not implemented**, so `thread.registered`
is `NOTAPPLICABLE` and **rule 11 is unverified for protoPython by that case.**
protoPython creates exactly two kinds of OS thread beyond the main one, both
through `ProtoSpace::newThread` (`src/library/ThreadModule.cpp:425` and `:698`),
and offers no hook to run arbitrary C++ on a live Python thread. Spawning one
here through `newThread` ourselves would audit protoCore's registration rather
than protoPython's — a pass for a property nobody checked. What is known instead,
by a different method: the static census finds **no thread creation outside those
two sites**, and `ThreadingStrategy::submitTask` runs inline with a worker count
of 0, so there is no pool. That is a reading, not a measurement.

## Judgement items

### C3 — Is any `ProtoObject*` held across an allocation only in a C++ local?

**Mechanised:** `gc.host_stress` — 20 rounds of the deque/threading path under a
400,000-cell ceiling, 21 cycles, 9,760,531 cells reclaimed. **Not yet run under
AddressSanitizer**, and until it is, that pass is not evidence.

**Not mechanised:** the general case — whole-program escape analysis over ~30k
lines. protoPython carries explicit GC-discipline comments and a
`transientArgsRoots_` root set created for exactly this purpose ("pins
native-call arg lists that live only in C++ stack locals",
`PythonEnvironment.cpp:16545-16549`), which is the right shape.

**One site found while reading, and filed rather than fixed:**
`src/library/ExecutionEngine.cpp:9426-9431` (`OP_LIST_TO_TUPLE`) creates a
mutable wrapper and then calls `setAttribute` and `addParent` on it **without
re-assigning from their return values and without rooting `tupObj` on the
operand stack**, across three allocating calls. `OP_BUILD_TUPLE` immediately
above does both — it pushes `tupObj` on the traced stack before those calls and
re-assigns from each result — and the divergence is the finding. protoCore
publishes a fresh state rather than mutating in place, so the discarded results
mean the `__data__` and `__class__` writes may be lost, and the unrooted wrapper
may be swept mid-construction. **Severity: high, unproven** — no failing test is
attached, and attaching one is the first thing to do with it.

**Reviewed:** agent, 2026-09-25 — **pending maintainer review.**

### C5 — Which structure is rule 5 measured on, and why?

**The tuple, deliberately.** protoPython has two user-visible sequences and they
have **different representations**: a list is a `ProtoList`, and a tuple is a
mutable wrapper over a `ProtoTuple` whose nodes are interned and perennial. There
is a third shape: the `*args` binding at `ExecutionEngine.cpp:609` binds a **raw
`ProtoTuple` with no wrapper and no `__data__`**, which
`test/regression/tuple_index_raw.py` documents as a live hazard. Measuring the
list would have made the case pass for the wrong reason — choosing the workload
to get the answer. The tuple is the representation rule 5 is about, so it is what
is measured, and the result is the 10% residual above.

**Reviewed:** agent, 2026-09-25 — pending maintainer review.

### C7 — Is external memory correctly accounted and correctly released?

**A real gap, not a formality.** protoPython has **15 `fromExternalPointer`
sites** and keeps **no byte total anywhere**, so `externalBytesAccounted()`
honestly returns −1 and the case reports `NEEDSREVIEW`. Most sites pass a real
finalizer (`delta_finalizer`, `deque_finalizer`, `mutex_finalizer`,
`file_buffer_finalizer`), so the C++ memory is freed; five pass `nullptr` and are
justified in `conformance-allow.txt` (four carry a pointer owned elsewhere, one
carries a C function pointer).

The consequence of keeping no total is concrete: a program holding a million
deques has a million untracked C++ allocations that do not count toward
`heapSize`, create no collection pressure and are invisible to the GC trigger.
`protoCore/docs/MemoryModel.md` §5 assigns that total to the embedder in as many
words. Whether protoPython should keep one is a maintainer decision.

**Reviewed:** agent, 2026-09-25 — pending maintainer review.

## Informational

- **protoPython never calls `setHeapLimits` and installs no
  `outOfMemoryCallback`**, so no collection cycle starts by itself and rule 8 was
  unreachable before this case existed. The repository already knows this: the
  comment at `CMakeLists.txt:1049-1056` explains that `PROTOCORE_HEAP_LIMIT_CELLS`
  is the only way to make protoCore collect.
- **`src/runtime/main.cpp:283`, `:366` and `:386`** poll
  `space->runningThreads` with `usleep(50000)` for up to 5 seconds, on the main
  thread, with **no `UnmanagedScope`**. The main thread is counted in
  `runningThreads`, so for the duration of that wait no collection can start —
  while the condition being waited on is a count of exactly the threads whose
  completion may need memory a collection would free. It does not hang only
  because of the `count < 100` escape hatch, after which it returns `EXIT_OK`
  **with workers still running** and destroys the environment — the outcome the
  loop's own comment says it exists to prevent. `stw.quorum_completes` does not
  catch this because it exercises steady state, not shutdown. **Severity: medium
  (a 5-second stall plus a use-after-free window at exit); two defects in seven
  lines, three times over.** The fix protoPython already has correct elsewhere is
  `py_shutdown` (`ThreadModule.cpp:732-756`): enumerate `space->threads` and
  `ProtoThread::join(ctx)` each one, which leaves the running set by itself.
- `protopy_import_site` is a pre-existing failure with **no skip or xfail marker
  anywhere in the repository** — it is registered with a `regression_gate` label
  and a timeout and nothing else. A failure documented only in a maintainer's
  memory is one refactor away from being attributed to this phase.
