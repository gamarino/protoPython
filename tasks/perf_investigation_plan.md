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

   **Session 3 (2026-05-05) progress:**
   - Both deferred changes landed in `protoCore:a01eeed7`:
     a. Inline 6-bit tag check replaces `proto::isObjectFast`
        in the chain-walk loop.  Saves one atomic load + one
        branch per chain step (the `getCellTypeRaw()` virtual).
     b. Dedup the per-iteration `toImpl<ProtoObjectCell>` call.
        `ocValue` defaults to `oc` (the immutable case where
        currentValue == currentPointer); re-computed only when
        the mutable_ref branch fires and the snapshot pointer
        differs.
   - **Verified the path #6 CAS-on-read hypothesis** — it is
     already a `load(memory_order_acquire)` on the read path;
     no CAS to remove.  Sub-task of path #6 closed.

   **Microbench impact** (Release `-O3 -DNDEBUG`, best-of-5):

   | Scenario                      | Pre-sess-3 | After   | Pre-revert target |
   | :---                          |       ---: |    ---: |              ---: |
   | getAttribute Hot Cache        |      ~13.2 |  ~10.1  |              8.19 |
   | hasAttribute Hot Cache        |      ~10.7 |  ~11.5  |              9.13 |
   | getOwnAttributeDirect         |       ~7.5 |   ~7.8  |             11.10 |
   | **10-level inheritance**      |      **~62** | **~45**|             36.73 |

   Both critical paths now within **~20 %** of the pre-revert
   microbench target.  binary_trees(10) wall-clock varied ±15 %
   between runs on this machine (CPU freq scaling / system load),
   so an honest update of the bench wall-clock awaits a CPU-
   pinned re-run on a quiet system.  The change is
   correctness-neutral so the win is whatever falls out.

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

   **Status (path #1, 2026-05-05): closed**.

   **Session 1** — mistakenly diagnosed as a `Cell::setNext`
   load-modify-store race (the load+store paired with concurrent
   flag-bit writers can drop one).  Slapped a CAS loop on setNext;
   stability went from 60 % to 95 %.  Committed as
   `protoCore:bdb63a26`.

   **Session 2** (after user pushback that the explanation didn't
   close — GC is single-thread, lastAllocatedCell chains are
   per-context, no obvious cross-thread setNext contender) —
   reviewed the writers to `next_and_flags` and found the actual
   architectural violation: `Cell::getCellTypeRaw()` lazy-filled
   the cached cellType bits via a `fetch_or`, called from
   `isObjectFast` from ANY caller (including mutator threads).
   That fetch_or was the cross-thread flag-bit write the CAS loop
   was guarding against.  Removed `getCellTypeRaw` entirely;
   isObjectFast falls back to the virtual `getType()` plus the
   tag check.  Committed as `protoCore:51459ce7`.

   The CAS loop in setNext is RETAINED as defense-in-depth.  In
   theory the architectural fix alone is sufficient (every
   non-GC writer to flag bits is now removed), but empirically
   removing the CAS loop drops stability from 100 % back to 70 %.
   The exact remaining mechanism is not identified — possibly a
   memory-ordering subtlety the load+store version misses, or a
   flag-bit writer not yet found.  Defense-in-depth is the honest
   call; CAS overhead is below noise on every microbench and
   rules out the entire failure mode regardless of its precise
   trigger.

   **Stability impact** (Release `-O3 -DNDEBUG`,
   `bench_binary_trees(10)` × 20 runs through the pyperf runner):

     Pre-path-#1 (lazy-fill + load+store setNext):  12 / 20  (60 %)
     Post-CAS only (still has lazy-fill):           19 / 20  (95 %)
     Post-architectural fix (no CAS, no lazy-fill): 14 / 20  (70 %)
     Both (CAS + no lazy-fill, current state):     **20 / 20  (100 %)**

   **Follow-up resolution (task #28, 2026-05-05): CAS removed.**

   Re-measured stability after path #2..#6 landed.  At the originally
   tested workload (`bench_binary_trees(10)`), the CAS no longer
   provides any benefit: 100 / 100 pass without it (was the 70 %
   condition above).  The original race was the cross-thread
   `getCellTypeRaw` lazy-fill, fully removed in `protoCore:51459ce7`.
   The CAS in `bdb63a26` was protecting against the SAME write
   (lazy-fill); once that write was removed, the CAS became redundant.
   The 70 % observation in the original measurement was likely
   timing-sensitive rather than evidence of a separate writer.

   `setNext` is back to a plain load+store with the comment in
   `headers/proto_internal.h` documenting the empirical re-measurement.

   **NEW finding (task #34) — RESOLVED in `protoCore:e0bed93d`**:

   A separate UAF reproducing 100 % at `bench_binary_trees(11)` was
   tracked down to a destructor ordering issue, NOT the survivor pen
   per se (the pen mechanism just amplified the window enough to
   surface the bug consistently at depth ≥ 11).

   Root cause: `ProtoContext::~ProtoContext()` did
     1. pop currentContext on the thread,
     2. submitYoungGeneration(lastAllocatedCell),
     3. allocate a ReturnReference in `previous` to anchor the
        returnValue in the parent's young chain.

   Step 2 publishes the inner context's chain to `dirtySegments` BEFORE
   step 3 anchors the returnValue in `previous`'s young chain.  Step 3's
   `new(previous) ReturnReference(...)` triggers an `allocCell` on
   `previous`, which parks at a STW poll.  If a GC fires here, Phase 4's
   root set is the parent's chain + globals + shard.root — and a fresh
   `Node()` returnValue with no `setAttribute` calls (i.e. no shard.root
   entry) is reachable ONLY through `this->returnValue`, which is no
   longer scanned because step 1 popped this context.  Phase 5 frees it.
   The ReturnReference allocation that finishes after the GC wakes ends
   up holding a freed cell pointer; the next time the recycled cell is
   dereferenced as a Node it has no `left` attribute (or any random
   attribute mismatch, depending on the type the slot was reused as).

   Fix: swap steps 2 and 3.  Anchor the returnValue first, THEN submit.
   One-line reorder; same allocations, same per-Node submit, no
   correctness side-effect on any other path.

   Verification (post-fix):
     bench_binary_trees(11): 0 / 30 → **100 / 100 PASS**
     bench_binary_trees(12): added — **10 / 10 PASS**
     bench_binary_trees(10): no regression (2.44–2.58 s)
     ctest: protoCore 165/165 + protoPython 163/163, conformity 9/10
     (unchanged).
3. **Path #3 — `gcThreadLoop` self time (17.9 %)** — partial close.

   **Session 1 (2026-05-05) progress:**

   Tried three approaches:

   a. **Merge child young chains into parent on destructor** to
      collapse the 95 700 segments/cycle (5.86 cells avg) into ~140
      segments/cycle (4 200 cells avg).  IMPLEMENTED, then REVERTED.
      The optimisation worked at the segment level (95 K → 140), but
      the trade-off didn't pay: cells live longer in young chains
      (parent's chain accumulates from all descendants until the
      outermost frame returns), Phase 2 root scan walks them every
      cycle (2 ms → 10 ms), and the live set per cycle GREW
      (5.6 M → 6.3 M cells marked) because dead cells stay reachable
      via the parent's young chain longer.  Net wall-clock got
      WORSE (~3 s → ~3.7 s).  Reverted.  Lesson: per-segment
      overhead is small (~50 ns/segment) compared to per-cell
      overhead (~93 ns/cell, dominated by cache miss); reducing
      segment count alone doesn't help.

   b. **Threshold sweep**: tried setting `PROTOCORE_GC_CONTEXT_THRESHOLD`
      to 100 K and 1 M to reduce GC cycle count.  Cycle count was
      UNCHANGED (10 cycles per bench at every threshold), because
      most submissions come from `ProtoContext` destructors (function
      returns), not the threshold.  The threshold tunable doesn't
      affect deeply-recursive workloads.

   c. **Prefetch in mark loop** (`__builtin_prefetch(nextPop, 1, 1)`):
      ~9 % improvement in Phase 4 mark (446 ms → 406 ms total over
      10 cycles), saving ~6 ns/cell.  Sweep prefetch tried and
      reverted — sweep already loads `getNext` on the current
      iteration, so the would-be prefetched load is already in
      flight.  Committed as `protoCore:e7650249`.

   **GC time breakdown after path-#1 + this session's mark prefetch**
   (Release `-O3 -DNDEBUG`, bench_binary_trees(10), 10-cycle profile):

   | Phase | Per-cycle | % of GC |
   |---|---:|---:|
   | P1 STW handshake | 11.8 ms | 10 % |
   | P2 root scan     |  2.3 ms |  2 % |
   | P4 mark          | 40.6 ms | 35 % |
   | P5 sweep         | 54.1 ms | 46 % |
   | P6 my unmark     |  8.5 ms |  7 % |
   | **Total**        | 117 ms  | 100 % |

   - Total bench: ~3.0 s, of which ~1.17 s is GC (39 %).
   - 562 K cells marked per cycle (avg ~5.58 M / 10).
   - 95 K segments swept per cycle (avg ~5.86 cells/segment).
   - Mark: 72 ns/cell (was 77 pre-prefetch); ~one L1 / L2 cache miss.
   - Sweep: 96 ns/cell; ~one L2 / L3 cache miss per cell.

   **Open follow-up — bump-pointer / per-arena cell allocation**:

   The remaining 39 % of bench wall-clock spent in GC is dominated
   by cache-miss-bound mark and sweep loops.  Cells are currently
   scattered across the per-context arena because the freelist /
   per-thread-pool allocator hands out whatever cell happens to
   be at the head of the free pool.  A bump-pointer allocator that
   places sequentially-allocated cells in adjacent 64-byte slots
   would let mark+sweep iteration walk linearly through cache lines
   instead of pointer-chasing into random heap.  Estimated impact:
   ~50 % reduction in mark+sweep cost (from ~93 ns/cell to ~30-40
   ns/cell), translating to ~15-20 % bench-wall-clock improvement.

   This is a non-trivial allocator rewrite — touches `getFreeCells`,
   the per-thread cell cache, the segment / survivor-pen handling,
   and possibly the cell layout itself.  Not in this session's
   scope; logged for a future dedicated session.
4. **Path #6 — `resolveMutableState` (4.23 %) + the CAS-on-read
   anomaly**.

   **Status (verified during path #2 session 3, 2026-05-05)**:
   the user's hypothesis is correct AND already the existing
   implementation.  `resolveMutableState` in `core/ProtoObject.cpp`
   (around line 33-79) reads `space->mutableRoot[shard].root` with
   `load(memory_order_acquire)` — pure atomic load, no CAS.  CAS is
   only on the WRITE path (when `setAttribute` publishes a new
   snapshot via `compare_exchange_weak`).

   So this sub-task of path #6 is **CLOSED — no change needed**.
   The 4.23 % CPU cost of `resolveMutableState` comes from:
     - The conditional `if (cache && cache[idx].mutable_ref ==
       mutable_ref)` cache check (one branch per call).
     - The `load(acquire)` itself + a comparison vs cached
       `shard_root` (one cache-line miss in the worst case).
     - The fall-back AVL probe `mutableList->implGetAt(...)` if
       cache misses (~10-50 ns).
     - Two `toImpl` calls (one on the thread, one on the live
       shard root SparseList) each with their full validation
       cascade.

   Future optimisations to chase here: bigger thread cache
   (currently MUTABLE_VALUE_CACHE_DEPTH entries, may thrash with
   many distinct mutables); inline the toImpl validation away
   for the hot path now that the type is structurally invariant.

   **Session 1 (2026-05-04) progress:**

   Stashed the per-thread `MutableValueCacheEntry*` array pointer
   directly in `ProtoContext::mutableValueCache_`, populated at
   context construction (both spawning constructor in `Thread.cpp`
   and `AdoptMainThreadTag` constructor for the root context).
   Eliminates the three-step indirection on every `resolveMutableState`
   call (`toImpl<ProtoThreadImplementation>(context->thread)
   → threadImpl->extension → mutableValueCache`) — replaced by one
   load (`context->mutableValueCache_`).  `refreshMutableCache`
   updated symmetrically.

   **Wall-clock impact** (Release `-O3 -DNDEBUG`,
   `bench_binary_trees(10)`, best-of-5):

     Pre-stash:  2.97 s
     Post-stash: 2.81 s ~ 2.86 s (~6 % faster)

   ctest: 150/150 (protoCore) + 163/163 (protoPython).
   Microbench unchanged (within noise) — `resolveMutableState`
   doesn't show up in the attribute-cache microbench, which is
   bound by the chain walk, not snapshot resolution.  The win is
   wall-clock-only because the savings amortise over every mutable
   attribute access on the bench's recursive `Node` tree
   construction.
5. **Path #5 — `getFreeCells` (7.18 %)** — allocator slow path.
6. **Path #4 — small `ProtoList` for argsList / call frames** ← in progress, session 1 done

   **Session 1 (2026-05-05) progress: protoCore plumbing landed.**
   - Added `ProtoListSmallImplementation` Cell (5 inline `ProtoObject*`
     slots + size, 56 B, fits one 64-byte Cell).  New tag
     `POINTER_TAG_LIST_SMALL = 25`, new `CellType::ListSmall`.
   - Added `ProtoContext::newSmallListN(n, items)` — single-allocation
     builder that produces a SmallList for `n ≤ 5` and falls back to
     the AVL builder otherwise.
   - Rewrote every `ProtoList::*` trampoline in `core/ProtoList.cpp`
     with one tag-dispatch at entry. Read-only ops read from
     `slots[]` directly; mutator ops produce a fresh output whose
     form is selected by the resulting size (≤5 → SmallList,
     >5 → AVL via a new bottom-up `buildBalancedFromArray` helper).
     Only `appendLast` overflowing past size 5 forces AVL output.
   - Iterator unified: `ProtoListIteratorImplementation::base` is
     now a tagged `const ProtoObject*`; `implNext` / `implAdvance`
     dispatch on the tag with no virtual call.
   - Tag fanout in `core/ProtoObject.cpp` (prototype lookup +
     `asList` cast), `core/LargeInteger.cpp` (`isCell`), and
     `core/ProtoContext.cpp` (`newTupleFromList` reads slots
     directly into a vector for SmallList input).
   - 15 new unit tests in `test/test_smalllist.cpp` covering form
     selection across every mutator, iterator round-trip on both
     forms, asObject/asList round-trip, tupleFromList over
     SmallList, and a `ProtoRootSet`-pinned GC stress that
     exercises `processReferences`.
   - All ctest green: protoCore **165/165** (150 prior + 15 new),
     protoPython **163/163**.  bench_binary_trees(10) unchanged
     (~2.8 s) — expected, since no frontend integrates SmallList
     yet.  Commit: `protoCore:<TBD>`.

   **Session 2 (planned):** integrate the new builder into
   protoPython OP_CALL_FUNCTION (and protoJS interpreter call
   paths) for the n ≤ 5 case.  That's where the actual
   wall-clock win materialises.

   **Session 1 design notes (preserved):**
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
