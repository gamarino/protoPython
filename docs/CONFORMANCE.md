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
| **`heap.ceiling_progress`** | **8** | **FAIL** (isolated), and it STAYS red | cause now established and it is protoCore's: every thread that exits permanently loses 4,096 cells from the free list. See below |

Suite: **632** ctest cases, **630 passing** (2026-09-25, after the py_bytes_find
fix). Both failures are known and neither is new: `protopy_import_site`, which
pre-dates this phase and has no skip marker anywhere in the repository, and
`conformance_isolate.heap.ceiling_progress`, whose cause is in protoCore. The
count rose from 631 to 632 with `protopy_bytes_find_needle_type`, the regression
that pins the dead `== nullptr` fix.

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

**Severity: high. MECHANISM NOW ESTABLISHED, and it is in protoCore, not in
protoPython.** It is therefore diagnosed here and **not fixed**: a kernel change
affects five runtimes and is the maintainer's call. The case stays red.

**Every protoCore thread permanently loses its private free-cell batch when it
exits.** `ProtoThreadExtension::~ProtoThreadExtension` frees the attribute cache,
the mutable-value cache and the `std::thread`, but never returns
`this->freeCells` to the space — unlike `ProtoContext::~ProtoContext`, which
returns its own batch under `globalMutex` a few lines of the same codebase away.
The cells are on no free list and are not live, so `heapSize - freeCellsCount`
grows permanently by one batch per thread created.

**Measured with a bare `ProtoSpace` and no runtime at all** — two threads per
round, created through `ProtoSpace::newThread`, joined, then six forced cycles —
`heapSize` constant at 262,144, `liveCellsLastCycle` constant at 306, and
`freeCellsCount` falling monotonically for ever:

| thread body | cells lost per thread |
|---|---|
| returns immediately | **4,096** |
| allocates (200 `newList` calls) | **8,192** |

The rule-8 workload creates **202 threads** (two per round, 101 rounds), so it
loses 827,392 cells at the low figure — four times the 200,000-cell headroom the
case leaves. **No protoPython-side change can make this case pass**, and reducing
the adaptor's thread count would hide a real kernel defect rather than fix one.

protoPython contributes a second, smaller term on top: ≈470 *marked* cells per
finished thread, which the bare-protoCore probe does not show
(`liveCellsLastCycle` stays flat there). That is protoPython's to find, and it is
not what breaks the ceiling. Measured over 20 rounds of 2 threads with only 20
items per round — so items cannot be the variable — `liveCellsLastCycle` rose by
939 cells per round, 476 per thread, linearly. The same probe with **no threads**
and 2,000 items per round holds `inUse` flat at +3,579 across 20,000 items, i.e.
**the items retain nothing**; the earlier candidate list had this the wrong way
round.

Of the three candidates the first report listed, candidate 1 is half right — it is
per-thread, but the Python-level bookkeeping is clean (`threading._active` and
`_limbo` are both empty after every round, verified with `protopy`), so it is the
kernel's thread teardown and not `threading.py`. Candidate 2 (module-global
retention) and candidate 3 (the P2 parking topology) are both ruled out: the
retention is proportional to threads and to nothing else.

**No fix is proposed, because the two obvious ones were tried and MEASURED not to
work.** Reported that way on purpose: an unvalidated one-liner in a kernel that
five runtimes share is worse than a precise observation.

What was tried, in a scratch clone of protoCore, and left the numbers bit-identical
(4,096 cells per empty-bodied thread, still monotonic):

1. Returning `ProtoThreadExtension::freeCells` to the space in
   `~ProtoThreadImplementation`.
2. Returning it at thread exit in `thread_main`, next to the deregistration that
   is already there — and, separately, returning the thread root context's own
   `ProtoContext::freeCells` there too.

So the batch is not where those two guesses put it, and the search should continue
from the measurement rather than from the code's shape.

**Two facts established by reading, which stand on their own whatever the 4,096
cells turn out to be.** Sweep calls `cell->finalize(...)` and never a C++
destructor (`core/ProtoSpace.cpp`), and `ProtoThreadImplementation::finalize` is
literally "Nothing to do here". So for a collected thread, neither
`~ProtoThreadImplementation` nor `~ProtoThreadExtension` ever runs: the thread's
root `ProtoContext` is never `delete`d, the `std::thread` object is never `delete`d,
and the `malloc`'d `attributeCache` and `mutableValueCache` are never `free`d.
Those are leaks in their own right, and the first of them is the one most likely to
be next to the cells.

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

## The defects the static check leaves uncovered

`conformance-allow.txt` **exits 1 on purpose.** It listed two; one is now fixed,
so the uncovered count is **1**.

### 1. FIXED — `src/library/PythonEnvironment.cpp:11426`, a dead `== nullptr` with a user-visible consequence

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

CPython raises `TypeError`. protoPython reported a match at index 0. The guard's
evident intent — "an empty needle from a non-bytes-like object means -1" — never
ran, so the call fell through to `haystack.find("")`, which returns `start`.

**Fixed 2026-09-25, and not by repairing that comparison.** The guard's own intent
(-1) is not CPython's behaviour either, so comparing against `PROTO_NONE` would
have replaced a wrong answer with a different wrong answer. The needle check moved
into `bytes_needle_validate`, which is now **complete** rather than `str`-only: it
accepts an integer or a value `bytes_view` can read (bytes, bytearray, memoryview,
`ProtoByteBuffer`, `array.array`) and raises
`TypeError("argument should be integer or bytes-like object, not '<type>'")` for
anything else. It also returns the needle it validated, so `bytes_view` — which
may call the argument's `tobytes()` — runs once per lookup instead of twice. The
dead comparison is deleted. `find`, `rfind`, `index`, `rindex` and `count` all go
through it.

```
$ protopy -c 'class W: pass
b"abc".find(W())'
TypeError: argument should be integer or bytes-like object, not 'W'
```

**Case: red to green.** `test/regression/bytes_find_needle_type.py`, registered as
ctest `protopy_bytes_find_needle_type`. **Mutation:** make
`bytes_needle_validate` return true for a value `bytes_view` cannot read (`return
true;` after the `bytes_view` branch, which is exactly what the dead comparison
achieved). The case fails; restored, it passes. The static check's uncovered count
goes 2 → 1.

**Found while writing that test, filed and NOT fixed:** `b"abcabc".find(b"bc", 0, 2)`
answers 1 where CPython answers −1. `py_bytes_find` compares the match *position*
against `end` instead of the match's *end* against `end`, so a needle that starts
inside the slice but runs past it counts as a match. The correct test is
`pos + needle.size() > end`. `py_bytes_rfind` slices first and does not share the
bug. It is a separate defect from the one this phase was asked to fix, and the
regression file says in a comment why it deliberately does not assert today's
answer.

### 2. OPEN — `src/library/PythonEnvironment.cpp:8668`, an always-true condition

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
