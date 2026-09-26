# protoPython — embedder conformance

The normative rule table is `protoCore/docs/EMBEDDER-CONFORMANCE.md`. This file
records protoPython's result, the judgement answers a script cannot give, and
what the adaptor does and does not prove.

- Adaptor: `test/library/ConformanceHost.h`
- Wiring: `test/library/ConformanceTests.cpp`, `test/library/ConformanceIsolate.cpp`
- Static ratchet: `conformance-allow.txt` (**exits 1 on purpose — one real defect left**)
- Retention regression: `test/library/TestThreadStateReleased.cpp`
- Run: `ctest --test-dir build_release -R 'conformance' < /dev/null`

## Result — 2026-09-25, protoCore 2.4.0 (SOVERSION 3)

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
| `stw.quorum_completes` | 2 | **PASS** (isolated) | a cycle advanced twice with a Python thread up (`runningThreads`=2); **20.5 s**, was 278.4 s |
| `join.parks` | 2b | **PASS** (isolated) | a cycle completed while `Thread.join()` was blocked, reclaiming 499,468 cells; **20.6 s**, was 278.1 s |
| **`heap.ceiling_progress`** | **8** | **PASS** (isolated) | 30 cycles completed while the workload ran, ~1.3 M cells reclaimed by the last one (run-variant: 1,306,839 / 1,293,491 / 1,303,077 across three runs — do not quote one as a constant), `oomCallbackFired=0`; 5.9 s. Red until 2026-09-25 — see below for the cause, which was NOT what this file previously claimed |

Suite: **639** ctest cases, **638 passing** (2026-09-25, after the retention
fixes below) — up from 636 of 638. The one remaining failure is
`protopy_import_site`, which pre-dates this phase and has no skip marker anywhere
in the repository. The count rose by one with `test_thread_state_released`, the
three-assertion regression that pins the retention fixes, and the second failure
went away because `heap.ceiling_progress` is now green.

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

## Rule 8: red to green, and a correction to what this file used to claim

### What this file said, and why it was wrong

Until 2026-09-25 this section read, in as many words:

> **Every protoCore thread permanently loses its private free-cell batch when it
> exits.** […] The rule-8 workload creates **202 threads** (two per round, 101
> rounds), so it loses 827,392 cells at the low figure — four times the
> 200,000-cell headroom the case leaves. **No protoPython-side change can make
> this case pass**, and reducing the adaptor's thread count would hide a real
> kernel defect rather than fix one.

**Every part of that conclusion was wrong, and the mistake is instructive.**

The kernel leak it described was real, and protoCore 2.4.0 fixed it (an exiting
thread now returns its batch; the cost went from −4,096 cells to −1). But an A/B
on the *identical* binary, pre-fix and post-fix, aborted with a **byte-identical**
message. So the batch leak was never what this case measured — and the reasoning
that said it was had a hole visible without any measurement at all:

- The escalation fires on `reclaimedLastCycle == 0` twice, **not** on heap growth.
- The `live set 196471` it prints is the **marked** count. A cell sitting in a
  leaked free-cell batch is on no free list *and is never marked*, so leaked
  batches cannot appear in that figure by construction.

The lesson recorded here on purpose: a number that is consistent with a
hypothesis (827,392 lost cells > 200,000 headroom) is not evidence for it when
the mechanism cannot reach the quantity being reported.

### What was actually measured, in three steps

**Step 1 — the per-finished-thread retention is real, and it is protoPython's.**
Measured with a workload whose only variable is the number of threads: the item
count per round was held at 10 and then raised ×100 to 1,000 with no effect on
the figure, so items retain nothing. Over 40, 80 and 160 threads the marked-cell
growth was linear at **302 cells per finished thread**. Three protoPython causes,
found by bisection and each fixed below: the process-global thread-roots dict,
`threading._dangling`, and one closure per `threading.Thread`.

**Step 2 — every function object protoPython creates at run time was immortal,
and that was most of the 302.** Same method, varying the number of function
objects instead of threads: **~62 marked cells retained per function object**,
flat across 30 further collection cycles, and *not* reclaimed by making the
object immutable nor by removing the metadata-cache pin. A breadth-first search
from protoCore's actual GC roots found the dropped function reachable in one hop
from a `ProtoSpace::mutableRoot` shard. The mechanism:

```
fn    --__closure_frames__-->  frame      (createUserFunction)
frame --co_name-------------->  fn        (OP_BUILD_FUNCTION binds the new
                                           function's name in the defining frame)
```

Both objects are mutable. protoCore keeps a mutable object's live state in
`mutableRoot[mutable_ref % 256]`, mark traces that table as a **root**, and the
entry is released only once the handle cell has been finalized
(`protoCore/docs/GarbageCollector.md`, "Phase 5b"). In a cycle each handle stays
marked through the other object's shard entry, so neither is ever finalized and
neither entry is ever released. **A reference cycle that includes a mutable
protoCore object is uncollectable.** Confirmed with a bare `ProtoSpace` and no
runtime: 1,000 objects of 8 attributes each, created and dropped, retained 2.73
marked cells each when immutable, **2.54 when mutable, and 12.29 when one
attribute was a `ProtoMethod` bound back to the object**. Filed as a kernel
finding below; **not fixed here**, because protoCore is out of scope for this
phase.

**Step 3 — and none of it is what kept rule 8 red.** This is the part that
matters, and it is why the fixes below are presented as their own result rather
than as the rule-8 fix. With the item count, the bounded backlog and the ceiling
all unchanged and the thread count set to **zero** — items produced inline, no
`threading.Thread` anywhere — the case still aborted, at a live set of **201,595
cells against 201,600 with its two threads per round**. Per-thread retention
moved the number by five cells.

What kept it red was the **ceiling calibration**, and the figure is stark.
The case settles the space, reads "the live set the RUNTIME itself needs", and
sets the hard ceiling 200,000 cells above it. protoPython imports its stdlib
lazily, so a cold Host settled at 49,152 cells — the interpreter before it has
ever seen `threading` — and then `runProducerConsumer`'s own source did
`import threading` and `from collections import deque` **inside the measured
window**. Measured with the case's exact sequence:

| point | `inUse` | note |
|---|---|---|
| settled, cold Host | 49,152 | what the case used to compute the ceiling |
| after `import threading`, `from collections import deque` | 159,503 | **110,351 cells, 55% of the whole headroom** |
| ceiling | 249,152 | leaving 89,649 cells for a 202-thread, 202,000-item workload |

Those 110,351 cells are permanently and legitimately live: they are
protoPython's threading stack, not garbage the collector failed to reclaim. The
case was comparing protoPython's live set against a ceiling set below it — the
exact miscalibration its own comment warns about ("Too low and the abort says
nothing about the runtime").

### What was changed

**The adaptor now warms the runtime in its constructor**
(`PythonConformanceHost::warmRuntime`): it imports `threading`, `time` and
`collections.deque` once, before the case settles, so that "the live set the
runtime needs" includes the runtime. Nothing else moves — **the threshold is
still `settled + 200,000`, the workload still produces 202,000 items through two
threads per round, and the thread count is untouched.** Result: settled live set
166,421, ceiling 366,421, **30 cycles completed while the workload ran,
1,306,839 cells reclaimed by the last one, `oomCallbackFired=0`, 5.9 s.**

**Both of those numbers are one run's, not constants.** Re-running gives
1,293,491 and 1,303,077 cells reclaimed, and a settled live set of 158,520
against the 166,421 above. Quote them as a range or re-measure; the verdict does
not rest on the exact value (`protoCore/docs/FIELD-NOTES.md`, case 9).

**Mutation that reds it again:** delete the `warmRuntime()` call from the
constructor. The case returns to `SIGABRT` with
`heap hard limit 249152 cells reached; live set 196534 cells, last cycle
reclaimed 0`.

**One weakness of this, stated rather than hidden.** With a warm Host,
`heapSize` has already grown to 1,572,864 cells by the time the case computes
`settled + 200,000 = 359,073`, so the hard ceiling is *below* the current
`heapSize` and therefore caps no further growth — the heap simply never grew
(`heapSize 1572864->1572864`) and the workload ran inside the free pool it
already had, reaching `inUse` 971,296. The property rule 8 asks about was
exercised (the heap could not grow, cycles completed, progress was made, no OOM),
but with more room than the case intends. **Recommendation for protoCore, not
actioned here:** give `Host` a `prepare()` hook that the case calls *before* it
settles, and compute the ceiling from the sample taken after it. That makes the
calibration sound for any runtime with a lazily-imported stdlib instead of
relying on each adaptor to warm itself in a constructor.

### The retention fixes, and what they measure

These stand on their own: each closes an unbounded leak, each is pinned by a
regression case, and together they take the per-finished-thread figure from
**302 marked cells to 107** (`threading.Thread` with no Python-level target;
151 when the caller also builds a lambda per Thread, as the measurement workload
does). The floor for comparison is **32.7 cells per thread** for a thread started
through `_thread.start_new_thread`, which bypasses `threading.py` entirely.

1. **`src/library/PythonEnvironment.cpp` — the per-thread `py_thread` record is
   a `ProtoRootSet` handle, released at thread exit.** It used to be pinned by
   storing it as an attribute of one process-global mutable object keyed
   `"thread_<std::thread::id>"`. Nothing ever removed the entry, and
   `ProtoObject::setAttribute` **strong-interns its key**, so each finished
   thread also left a permanent Symbol in the kernel symbol table — which is why
   "also erase the entry" would have been only half a fix. A `ProtoRootSet`
   needs no key: `add` returns a handle, `remove` releases it, and
   `pyThreadRoots_->size()` is now a checkable invariant.
   `PythonEnvironment::releaseThreadState`, called at the end of
   `thread_bootstrap`, drops that pin, drains any active-exception pins an
   escaping exception left unmatched, and clears every thread-local raw
   `ProtoObject*` mirror.

   **This one is small in cells** (~5 per thread, below the noise of the
   slope measurement) **and large in kind**: it was unbounded in both the dict
   and the symbol table.

2. **`src/library/ExecutionEngine.cpp` — a function object keeps its defining
   frame only when its own bytecode could use it.** `createUserFunction` now
   scans the native bytecode once and installs `__closure_frames__` only when the
   body contains `OP_LOAD_DEREF` (it can read a free variable) or
   `OP_BUILD_FUNCTION` / `OP_BUILD_CLASS` (it can define something that will).
   When the bytecode is unavailable the answer is "yes", because a conservative
   extra reference costs memory and a missing one would break name resolution.
   **The criterion is `co_freevars`, not the opcode census**, and getting that
   wrong the first time is worth recording: a first version gated on
   `OP_LOAD_DEREF` alone, and `def f(): nonlocal a` has a free variable while
   emitting no `OP_LOAD_DEREF` at all, because it never reads `a`. That dropped
   its frame, `f.__closure__` came back `None`, and `types.py`'s `_cell_factory`
   — which is exactly that shape — produced the wrong `types.CellType`.
   `protopy_closure_cells` caught it. The compiler stamps `co_freevars` with "the
   exact set of names this function AND ITS NESTED SCOPES close over from
   enclosing function scopes", so a non-empty tuple is the authoritative answer
   and covers reads, writes and nested definitions alike; the opcode flags stay
   as a conservative belt for a code object that carries no `co_freevars`.

   `OP_BUILD_FUNCTION`'s frame-refresh path is gated on the function actually
   having the attribute, so it cannot put the cycle back. The metadata cache's
   `hasClosure` flag now reports what the graph *is* rather than whether a frame
   was passed in, which keeps it consistent with the call path's existing
   `cacheNoLoadDeref` shortcut — protoPython already skipped the closure frame at
   call time for exactly these functions.

   Also removed while in that function: the per-instance `__call__` and `__get__`,
   which were `fromMethod(fn, …)` — a **direct** self-reference, immortal by the
   same mechanism. The function prototype already carries both as
   `fromMethod(nullptr, …)`, dispatch passes the receiver explicitly, and
   `invokeCallable`'s fast path for user functions never reads `__call__` at all
   (it recognises an own `__code__`). The bootstrap path, which has no prototype
   to inherit from, still installs its own pair.

   **Measured: ~62 → ~4.6 marked cells per dropped function object** (800
   lambdas created and dropped).
   **Mutation:** `const bool needsClosureFrame = true;`. The slope returns to
   **58.5** cells per function and
   `test_thread_state_released.DroppedFunctionObjectsAreReclaimed` fails.

3. **`lib/python3.14/_weakrefset.py` and `lib/python3.14/threading.py` — two
   documented stdlib deviations, both forced by protoPython having no GC-level
   weak reference.** `_weakref.ref(obj)` registers `obj` in the `_weakref`
   module's `__active__` presence set, and that membership is a **strong**
   reference released only by `_weakref._evict` (`src/library/WeakrefModule.cpp`
   says so in its header comment). A `WeakSet` that never evicts is therefore an
   unbounded leak rather than a weak container: `threading._dangling` grew by one
   permanently-reachable `Thread` per `Thread` ever created. The BFS from the GC
   roots found a finished, dropped `Thread` reachable as
   `mutableRoot[shard] → __active__ → [sparse] → Thread`.

   - `WeakSet.discard` / `remove` / `pop` / `clear` now evict. Membership is what
     keeps an element alive, so leaving the set releases it. That makes this
     `WeakSet` as weak as protoPython can express — deterministic on removal
     instead of automatic on collection. It does **not** make an element vanish
     when the last other reference goes away.
   - `Thread._delete()`, which already runs in `_bootstrap_inner`'s `finally`,
     now also does `_dangling.discard(self)`. `_dangling` keeps its documented
     purposes (debugging, `_after_fork`) for threads that are alive or were never
     started; a thread that has finished needs neither.
   - `Thread.__init__` now shares **one** `invoke_excepthook` closure instead of
     building one per Thread. That closure captures free variables, so it is
     caught by the mutable function ↔ frame cycle above: **117 marked cells per
     Thread created**, measured on its own. Everything it captures is
     module-global and identical for every Thread (`excepthook`,
     `_sys.excepthook`, `print`, `_sys`), and `invoke_excepthook` reads the live
     `excepthook` global first, falling back to the captured one only when that
     is None — so the only observable difference is *when* `old_excepthook` was
     captured.

   **Measured: ~224 → 107 marked cells per finished thread** for a Thread with
   no Python-level target. **Mutation:** remove `_dangling.discard(self)` and
   restore the per-Thread `_make_invoke_excepthook()`. The slope returns to
   **227.9** cells per thread and
   `test_thread_state_released.FinishedThreadsDoNotAccumulateMarkedCells` fails.

### Regression case

`test/library/TestThreadStateReleased.cpp`, registered as ctest
`test_thread_state_released` (three cases, 4.7 s, `regression_gate`). It asserts
a **slope**, never an absolute: the thread count or function count varies while
the round count is held fixed, so the per-round cost and the one-off cost of
compiling the snippet are identical in both runs and cancel. Absolute figures
would have pinned an allocation count; the slope pins the leak. It shares one
`PythonEnvironment` across its cases because protoPython is one runtime per
process — a second instance comes up with no import system.

### Kernel finding: a reference cycle through a mutable object is uncollectable

**Severity: high. Reported, not fixed — protoCore is out of scope for this
phase, and this needs the collector's owner.**

`ProtoSpace::mutableRoot` is traced as a mark root, and an entry is released only
when the mutable object's handle cell is finalized. A cycle `A → B → A` in which
either object is mutable therefore keeps both handles marked for ever: neither is
finalized, neither entry is released. Ordinary cycles among immutable cells are
collected correctly; this is specific to the mutable-shard table.

**Reproduction, no runtime involved** — 1,000 objects of 8 attributes each,
created through `newChild`, dropped, then cycles driven to convergence:

| population | marked cells retained per object |
|---|---|
| immutable | 2.73 |
| mutable, no self-reference | 2.54 |
| mutable, one attribute a `ProtoMethod` bound back to the object | **12.29** |

The consequence for embedders is not narrow. Any embedder whose objects are
mutable and whose object graph has cycles leaks in proportion to the cycles it
creates. protoPython's Python functions were the instance found here, and it took
three separate protoPython-side changes to stop *creating* the cycles rather than
to fix the cause. `docs/GarbageCollector.md` § "Phase 5b" documents the release
rule and its soundness argument, and that argument is correct — the gap is that
the rule cannot fire for a cycle.

**Secondary observation, low confidence, offered with the repro rather than as a
claim:** the same probe at 2,000 self-referencing mutable objects aborted inside
the collector with `CRITICAL TAGGED POINTER (Phase4(young)): … type 23`,
reproducibly and at the same address across runs. The probe pins a `ProtoMethod`
cell in a `ProtoRootSet`, which may not be a supported thing to do; it is
recorded so the maintainer can decide, not asserted as a defect.

## The two slow cases are now bounded by the clock, not by throughput

`join.parks` and `stw.quorum_completes` passed at **278.08 s** and **278.43 s**
against ctest's 300 s budget — a 22-second margin on an idle machine, and both
were observed timing out when the machine was busy. A test that fails because
another process is busy measures the other process.

**Chosen: shrink the workload, by changing what bounds it.** The budget is
untouched. `Host::joinBlockingThread` used to run a fixed 400,000 iterations of
`total += len('spin-%d' % i)`; it now allocates in the same way but stops on
`time.monotonic()` after **20 s**.

The property survives, because neither case ever cared how much arithmetic the
worker got through. Both need exactly one thing from this capability: that a
registered, allocating protoPython thread is still up while the case demands a
collection. `stw.quorum_completes` waits up to 5 s to see the thread in the
running set and then drives at most 2 cycles with a 15 s deadline; `join.parks`
gives a collection 8 s to complete and releases after 10 s. 20 s is twice
`join.parks`' release timer and outlives every deadline either case sets.

Bounding by the clock is strictly better than a smaller iteration count, which is
why it was preferred: under load the worker completes fewer iterations and still
stays up for 20 s, so the case's duration no longer depends on machine load or on
protoPython's throughput at all. The inner fixed-length loop of 500 iterations
keeps the clock read off the allocation path — the worker must allocate
continuously to reach protoPython's every-64-opcodes safepoint, and a
`time.monotonic()` call per allocation would make the syscall the thing being
measured.

| case | before | after | verdict |
|---|---|---|---|
| `join.parks` | 278.08 s | **20.54 s** | PASS, `cyclesCompletedWhileJoinBlocked=1`, 499,468 cells reclaimed |
| `stw.quorum_completes` | 278.43 s | **20.47 s** | PASS, `gcCycleCount` advanced by 2 with `runningThreads=2` |

Margin against the 300 s budget: 279 s, and it is no longer a margin that a busy
machine can eat.

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
  unreachable before this case existed. That is also why the rule-8 result is a
  statement about a ceiling protoPython never sets for itself. The repository already knows this: the
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
