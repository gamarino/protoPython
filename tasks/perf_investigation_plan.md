# Multi-session perf investigation plan

**Started**: 2026-05-05
**Trigger**: `binary_trees(10)` 5× regression diagnosed and partially fixed
(commit `protoCore:77751bc2`).  The pre-mark `std::set` was the dominant
cost; replacing it with O(N) post-sweep unmark recovered the bench to
the 2026-05-01 baseline ratio (~85× vs CPython 3.14, was 80×).

After that fix, the post-fix `perf record` shows the remaining
distribution.  Six paths are worth chasing, **all of them**, in the order
agreed below.

## Order of work (agreed)

1. **Path #2 — `getAttribute` (11.49 % of CPU)**  ← in progress, session 1 done

   **Session 1 (2026-05-05) progress:**
   - Read the revert commit (`84974040`) and the seven reverted
     attr-cache commits (`861cfe43..f0bfbcaa`).  Captured the bug
     mechanism in `tasks/perf_investigation_plan.md` (this file).
   - Verified the post-revert binary does NOT exhibit the
     `_weakref` after `importlib.machinery` corruption.
   - Added a permanent regression test:
     `tests/conformity/import/test_attr_cache_cross_module.py`,
     wired into `tests/conformity/bootstrap/cpython_bootstrap.txt`.
     The test covers four invariants that any future cache MUST
     keep green:
       1. No empty-string attribute names.
       2. Canonical `_weakref` attributes (ReferenceType,
          CallableProxyType, ProxyType, ref, proxy, getweakrefs,
          getweakrefcount) all present.
       3. No `importlib.machinery` internals (`_check_name`,
          `_classify_pyc`, `_compile_source_to_code`) leak in.
       4. `getattr(_weakref, name)` round-trips for each advertised
          name (catches "value-substituted" cache hits that pass
          the dir() check).

   **Session 2 (2026-05-05) progress:**
   - Confirmed that the reverted chain only touched two files
     (`core/ProtoObject.cpp` + `headers/proto_internal.h`) — no
     SymbolTable or ProtoString changes.
   - Diff'd HEAD against the pre-revert tip (`f0bfbcaa`) and
     identified that current code already has the cache STRUCTURE
     and per-step OWN caching design (re-introduced incrementally
     after the revert by 0dd7c24c, 36db54d3, etc.).  The two
     optimisations actually missing were:
       a. **Negative caching**: cache misses (own-attribute not
          present) were not stored.  A 10-level inheritance walk
          paid 10 × AVL probe per call.
       b. **Direct-field parent-chain navigation**: a `validLink`
          helper did a virtual `getType() == ParentLink` check plus
          PLT calls through `getParent(context)` / `getObject(context)`.
          ~2 virtual calls per chain step, ~20 ns × 10 steps per
          inherited lookup.
   - Both landed in `protoCore:8bbfafe7`.

   **Microbench impact (Release `-O3 -DNDEBUG`):**

   | Scenario                      | Pre-fix | After  | Pre-revert target |
   | :---                          |    ---: |   ---: |              ---: |
   | getAttribute Hot Cache        |   11.71 |  ~13.2 |              8.19 |
   | hasAttribute Hot Cache        |    9.83 |  ~10.7 |              9.13 |
   | getOwnAttributeDirect         |    6.78 |   ~7.5 |             11.10 |
   | **10-level inheritance**      |   88.98 | **~62**|             36.73 |

   The 10-level inheritance recovered **~30 %** toward the pre-revert
   target.  Hot-cache cases are essentially flat (within noise).
   `binary_trees(10)`: 3.04 s → 2.87 s (−5 %).  Two thirds of the
   gap to the 2026-05-01 baseline (2.18 s) closed.

   **Session 3+ — remaining gap to pre-revert:**

   The 10-level case is still 62 ns vs the 36.73 ns target.  Two
   reverted-chain optimisations not yet re-applied:
   1. **Hot-cache fast-path entry**: the reverted code had a
      separate "first-iteration cache probe" that returned the
      cached value WITHOUT entering the chain-walk loop at all
      when the cache hit on the original `this`.  Saves the
      isObjectFast / mutable-snapshot resolve / loop overhead on
      the hot path.  This is the missing 4-5 ns on
      `getAttribute Hot Cache` (8.19 → 13.2).
   2. **`(ptr & 0x3F) == OBJECT` instead of `isObjectFast`**:
      isObjectFast does the tag check AND a `getCellTypeRaw()` call
      (which is a virtual on first cell touch, then cached in the
      cell's flags).  The reverted code dropped the cell-type check
      because the chain navigation already invariantly visits
      ProtoObjectCell pointers.  ~1-2 ns per chain step.

   These are smaller, safer changes — the regression test will
   still gate them.  Scope for a 30-min session.

   **Sessions 4+ — the original pre-revert design:**

   The reverted design's failure mode (per commit `11d89fa8`
   message — the 3 bugs ALREADY fixed before the cross-module bug):

     a. CACHE_FLAG_OWN at bit 3 (0x8) aliased POINTER_TAG_SPARSE_LIST
        and the 8..15 tag range.  Stripping the flag at read silently
        re-tagged values as POINTER_TAG_OBJECT → SIGSEGV.  Moving
        the flag to bit 5 fixed the collision but kept the ambiguity.
     b. `hasAttribute` populated the cache without the OWN flag,
        but `hasOwnAttribute` / `getOwnAttributeDirect` checked the
        flag and reported "not own" on what should have been a hit.
     c. Mutation of an intermediate parent class did not invalidate
        the descendant's cache slot under start-object caching.

   The `11d89fa8` "own-only redesign" addressed (a)-(c) but
   introduced the cross-module key corruption that triggered the
   final revert.  The exact mechanism of the cross-module corruption
   is NOT yet diagnosed — the user's reproducer (`import
   importlib.machinery; import _weakref; print(dir(_weakref))`)
   shows 3 empty strings and a leaked `_check_name`, which suggests
   the canonical-symbol lookup at the START of getAttribute /
   setAttribute returned the wrong canonical symbol pointer for
   some keys.  That points at `SymbolTable::lookupByContent`, NOT
   the cache itself.  Future work should:
     1. Re-apply only `861cfe43` (Optimize attribute resolution
        engine).  Run the regression test.  Pass → continue;
        fail → bisect within that commit.
     2. Re-apply `c9ec9862` (Implement non-lossy attribute cache
        and O(1) chain resolution) on top.  Run the regression test.
     3. Re-apply `11d89fa8` (Redesign as own-only).  Run the
        regression test.
     4. Re-apply `f0bfbcaa` (Widen tag check from 3 to 6 bits).
        Run the regression test.
     5. The first commit that breaks the regression test isolates
        the layer where the cross-module corruption was introduced.
     6. Read the diff of THAT commit only, find the broken
        canonicalization, and fix it.

   Microbenchmark target on success:
     - getAttribute hot cache: 11.71 ns → 8.19 ns (~30 % faster)
     - inherited 10-level: 88.98 ns → 36.73 ns (~2.4× faster)
     - binary_trees(10): expect ~30-50 % wall-time drop on the
       OOP-dispatch-heavy bench.
2. **Path #1 — flake / UAF in survivor-pen** (correctness; 40 % crash on
   `bench_binary_trees(10)`).  Pre-requisite: must clear before any
   other perf experiment can be trusted (every run risks being a
   silent corruption).
3. **Path #3 — `gcThreadLoop` self time (17.9 %)** — instrumentation
   done in commit `wip-perf-profile`, see Findings below.
4. **Path #6 — `resolveMutableState` (4.23 %) + the CAS-on-read
   anomaly**.  *User hypothesis to verify*: the readers of the
   mutable-root cache only need `atomic_load(acquire)`, not CAS.  CAS
   is for writer/writer coordination; for a snapshot model where the
   stored state is the value itself, a pure load is correct (you read
   either the old or the new state, never an inconsistent
   intermediate).  If the code does CAS-protected reads, that is
   wasted atomicity work and should be fixed.
5. **Path #5 — `getFreeCells` (7.18 %)** — allocator slow path.
6. **Path #4 — small `ProtoList` for argsList / call frames**
   (originally drafted as "tiny-attr SparseList inline storage", but
   that idea was rejected — small SparseList ≤ 4 entries does not
   pay back the design cost because attribute storage is dominated
   by classes with many attributes, not few).

   **Better target**: a fixed-size inline ProtoList Cell that holds
   up to **5 `ProtoObject*` slots plus an inline count field**, with a
   variadic constructor on `ProtoContext` so a 3-arg call site can
   build its `argsList` in a SINGLE allocation from ProtoContext —
   no `appendLast` chain, no AVL spine, no per-element tree rebalance.

   Rationale (from user feedback 2026-05-05):
   - Method invocation `argsList` is the dominant ProtoList consumer.
     `OP_call` / `OP_call_method` / `OP_call_constructor` each build
     an argsList by `appendLast` per argument; an N-arg call costs
     O(N) appendLast × O(log N) per append = O(N log N) cell
     allocations plus per-tree rebalances.
   - Real-world arg counts cluster at 0–3.  Most calls would never
     need the AVL path — the inline 5-slot cell covers them
     completely.
   - This affects `ProtoListImpl ctor 3.39 %`,
     `processReferences 1.42 %`, `appendLast` (folded into the
     ctor cost), plus the indirect cost on the GC walk and on
     allocator pressure (one cell per call vs ≤ N cells per call).
   - Possible bonus: the small list could share the existing
     ProtoList's `getSize` / `getAt` interface so callers (the
     interpreter dispatch, native methods, builtins) need no
     branch on representation — same virtual call surface,
     different concrete impl.

   Concrete API sketch:
   ```cpp
   // In ProtoContext.h:
   const ProtoList* newSmallList(
       const ProtoObject* a0,
       const ProtoObject* a1 = nullptr,
       const ProtoObject* a2 = nullptr,
       const ProtoObject* a3 = nullptr,
       const ProtoObject* a4 = nullptr);
   // Or variadic:
   const ProtoList* newSmallListN(unsigned n, const ProtoObject* const* items);
   ```

   The cell carries 5 `ProtoObject*` + count + the standard Cell
   header.  Total = 16 + 5*8 + 8 = 64 bytes — fits exactly one
   64-byte cell.  No external storage.  When `appendLast` would
   push past 5 elements, transparently promote to the existing
   AVL representation (single conversion, amortised away).

   Original "tiny-attr SparseList" idea preserved for context but
   deferred — the SparseList path's ~12 % cost is real but spread
   across many small wins, while the small-ProtoList path is a
   single large win with a clean inline-vs-promoted structural
   boundary.

## Findings carried forward

### From the binary_trees(10) sub-phase profile (path #3)

10 GC cycles in a 3.15 s bench iteration.  Per-cycle averages
(microseconds):

```
P1 STW handshake   :   11 287 us  (9 % of GC)
P2 root scan       :    2 413 us  (2 %)
P4 mark            :   44 656 us  (36 %)
P5 sweep           :   55 103 us  (45 %)
P6 my unmark       :    8 749 us  (7 %)
```

- Bench time spent in GC: **39 %** (1.22 s of 3.15 s).
- ~561 K cells marked per cycle.
- ~95 700 segments swept per cycle, **5.86 cells / segment** — atrocious
  fragmentation.  The most likely cause is per-`ProtoContext`
  destructor submission of the young chain, which fires once per
  function-call frame and packages tiny chains into their own
  segments.  Worth investigating whether ephemeral contexts can pass
  their young chain to the parent context instead of submitting.
- Per-cell cost: ~80 ns mark + ~98 ns sweep = ~178 ns/cell.  That is
  consistent with one cache miss per cell (the cells are scattered
  across the heap; `processReferences` walks pointers that probably
  miss L2).

The instrumentation is gated by env var `PROTOCORE_GC_PROFILE=1` and
prints every 5 cycles (commit `wip-perf-profile` in protoCore).
Decision deferred: keep as runtime-toggleable feature or convert to
`#ifdef PROTOCORE_GC_INSTRUMENT` compile-time gate (no cost when
disabled).

### From the binary_trees(10) main profile (paths #1–#6)

Top consumers in `perf record -F 999 -g --call-graph=dwarf`, post my
pre-mark fix (only entries ≥ 1 %):

```
17.91 %  gcThreadLoop self                                    [path #3]
11.49 %  ProtoObject::getAttribute                            [path #2]
 7.18 %  ProtoSpace::getFreeCells                             [path #5]
 4.23 %  resolveMutableState                                  [path #6]
 3.39 %  ProtoSparseListImplementation::ProtoSparseListImpl   [path #4]
 3.37 %  ProtoSparseListImplementation::implGetAt             [path #4]
 3.36 %  ProtoObject::isString
 3.17 %  protoPython::executeBytecodeRange                    (interpreter)
 2.95 %  gcThreadLoop lambda #2 (mark callback)               [path #3]
 2.80 %  ProtoSparseListImplementation::processReferences     [path #4]
 2.30 %  ProtoContext::allocCell                              [path #5]
 1.78 %  ProtoContext::addCell2Context                        [path #5]
 1.47 %  ParentLinkImplementation::processReferences          [path #3]
 1.38 %  ProtoObject::hasOwnAttribute
 1.29 %  ProtoSparseListImplementation::implSetAt             [path #4]
 1.21 %  rebalance (ProtoSparseList AVL)                      [path #4]
 1.10 %  ProtoObject::getHash
 1.05 %  __tls_get_addr
 1.03 %  ProtoObject::isTuple
```

Reproduce:
```
perf record -F 999 -g --call-graph=dwarf \
    protoPython/build_release/src/runtime/protopy /tmp/bt_min.py
perf report --stdio --no-children -n --percent-limit 1
```

`/tmp/bt_min.py` is `benchmarks/pyperf/bench_binary_trees.py`'s
`workload(10)` plus a single `time.perf_counter()` measurement.

### Existing `getAttribute` baseline numbers

Microbench (from `protoCore/README.md` Performance Validation):

| Scenario                          | Latency (ns/op) |
| :---                              |             ---: |
| `getAttribute` hot cache          |           11.71 |
| `hasAttribute` hot cache          |            9.83 |
| `getOwnAttributeDirect`           |            6.78 |
| Inherited attribute (10-level)    |           88.98 |

Pre-revert (the May 2026 attr-cache rework that was reverted because
it corrupted cross-module string attributes — see memory
`project_protocore_attrcache_regression_may2026`):

| Scenario                          | Latency (ns/op) |
| :---                              |             ---: |
| `getAttribute` hot cache          |            8.19 |
| `hasAttribute` hot cache          |            9.13 |
| `getOwnAttributeDirect`           |           11.10 |
| Inherited attribute (10-level)    |           36.73 |

The hot-cache delta is ~30 % (11.71 → 8.19 ns); the bigger headline
is the inherited-attribute walk (88.98 → 36.73 ns, ~2.4× faster).

### The flake bug (path #1)

`bench_binary_trees(10)` invoked through the pyperf runner (1 warmup
+ 5 timed iterations under `PROTOCORE_GC_REINCLUDE_SURVIVORS=ON`,
the default) **flakes ~40 % of the time** with `'dict' object has no
attribute 'check'` or similar AttributeError indicating a UAF.

Verified: same 40 % rate exists with the pre-fix code (the slow
pre-mark pass just masked it under the 60 s pyperf timeout).
Disabling `PROTOCORE_GC_REINCLUDE_SURVIVORS` clears the crash but
reintroduces leaks.

The bug is in survivor-pen / sweep / setNext-vs-mutator interaction.
Likely UAF when:
1. Cell C is marked in cycle N, in markedList.
2. Sweep moves C to survivor pen (calls `cell->setNext(survHead)`).
3. Mutator concurrently does `setNext` on C indirectly via context
   young-chain manipulation (`addCell2Context`).
4. The non-atomic load-modify-store in `Cell::setNext` loses one of
   the two writes; either the chain is corrupted (visible later as a
   freed cell appearing as live-and-Wrong-type) or the mark bit is
   lost (which the original 0441247e fix was meant to prevent).

Investigation approach:
- Reproduce under AddressSanitizer build: `cmake -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" ..`
  Run pyperf binary_trees until ASAN reports.  This gives the
  immediate stack of both the use and the free site.
- Alternatively, `rr record` the run, replay backward from the crash
  to the first write that corrupted the cell.

## Audit / regression-gating context

Before merging anything that touches GC or attribute lookup:

- `ctest -j$(nproc)` against `protoCore/build_release` and
  `protoPython/build_release`.  Currently 150 / 150 and 165 / 165.
- `python3 tests/conformity/run_conformity.py` against the new
  protopy binary.  Currently 8 / 9 (test_dict_conformity is
  pre-existing, unrelated).
- `python3 tests/synthetic/sp_audit_truth.py --out
  docs/audits/audit_$(date +%Y-%m-%d).md`.  Currently 4 PASS / 2
  SILENT_HALT / 11 CRASH / 2 TIMEOUT.
- `protoJS` is also affected — `protoCore` is shared.  Run protoJS's
  tests/run_all_tests.sh before committing protoCore changes.

## Open architectural questions accumulated during investigation

1. **`Cell::setNext` is not atomic w.r.t. flag bits**.  Load-modify-
   store with the flags pulled from the loaded value; if any other
   thread does `fetch_or` / `fetch_and` between the load and store,
   that other write is silently overwritten.  Probably the root
   cause of the path #1 flake.  Fix is a CAS loop, ~2× setNext cost
   in the contended case but no cost in the uncontended case.

2. **CAS-on-read in mutable-root cache** (path #6).  User
   hypothesis: in a snapshot model where the writer publishes via
   CAS, readers only need `atomic_load(acquire)`.  CAS is wasted
   work for read-only access.  Verify by looking at
   `resolveMutableState` and any sites it routes through; if they
   indeed use `compare_exchange_weak` on read paths, replace with
   `load(acquire)`.

3. **Per-context young-chain submission granularity** (path #3
   sub-task).  95 K segments / cycle implies submissions every few
   cells.  If ephemeral function-frame contexts can pass their
   young chain to the parent at destructor time (instead of
   submitting), sweep cost drops proportionally.

4. **Cell co-location vs. random-heap allocation** (path #3 sub-
   task).  Mark + sweep pay ~178 ns/cell, dominated by cache
   misses.  Bump-pointer allocation per segment (or per-arena
   chunks) would make sweep iteration cache-friendly.  Big change.
