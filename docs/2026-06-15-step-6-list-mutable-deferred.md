# Step #6 — list-mutable-when-owned

**Date**: 2026-06-15
**Status**: **DEFERRED — needs a protoCore design decision, not a protoPython tactical fix**.

## Hypothesis (from the diagnosis document)

`list_append_loop` is dominated by allocator + GC pressure: 6.96 % `gcThreadLoop`, 6.50 % `getFreeCells`, 4.85 % `processReferences`, 2.82 % `ProtoListImplementation` ctor. Every `list.append(item)` rebuilds the persistent list (O(log N) cells per call), and 10 K appends pressure the concurrent collector enough that its background thread shows up as a separate hot symbol.

The proposed fix was a "mutable-when-owned" heuristic: when the bytecode analysis can prove the list reference is owned by the current frame (single-use, no aliasing escapes via STORE_ATTR / STORE_GLOBAL / yield / return-of-collection-by-reference), lower `list.append` to in-place mutation instead of persistent rebuild.

Expected impact: **≈ −80 % on `list_append_loop`** (close to native `std::vector::push_back`).

## Why this is not landing today

protoCore's ProtoList API is fundamentally immutable. Every documented mutator returns a new persistent list sharing structure with the input:

```cpp
const ProtoList* appendLast (ProtoContext*, const ProtoObject*) const;
const ProtoList* appendFirst(ProtoContext*, const ProtoObject*) const;
const ProtoList* insertAt   (ProtoContext*, int, const ProtoObject*) const;
const ProtoList* setAt      (ProtoContext*, int, const ProtoObject*) const;
const ProtoList* extend     (ProtoContext*, const ProtoList*) const;
// …all return a new ProtoList; the receiver is never mutated.
```

There is no `append_in_place(...)` or equivalent. The kernel commits to persistent semantics for ProtoList by design — it is one of the principles called out in `protoCore/CLAUDE.md`:

> Immutability by default — all collections (ProtoList, ProtoString, ProtoTuple) are immutable; modifications return new versions via structural sharing (AVL trees / ropes).

To get O(1) amortised append at protoPython level, ONE of the following has to change:

1. **protoCore exposes a mutable list backend.** Add a `ProtoMutableList` class with `append`, `pop`, `setAt` in-place semantics. Then protoPython's `OP_LIST_APPEND` can downgrade to this backend when its escape analysis proves the receiver is owned.
2. **protoPython implements its own list backing.** Replace the `ProtoList`-under-`__data__` wrapping with a `std::vector<const ProtoObject*>` (or similar) anchored from the wrapper. Breaks the "everything is protoCore" property, requires a separate GC bridging story.
3. **Bytecode-level escape analysis + amortisation.** Accumulate a small batch of `append`s into a buffer attached to the wrapper, flush to a single persistent `extend` call at the next read or scope exit. This is a tactical hack that captures most of the benefit without breaking the immutable kernel contract — but it is invasive (every operation that reads the list has to remember to flush).

None of these is a single-PR change. Option 1 is the architecturally correct path; it should be a protoCore RFC and a parallel patch to every runtime that hosts mutable list semantics (protoPython, protoJS, protoClojure's transient maps).

## What this commit ships

This commit is documentation only. It records the investigation so a future reader of the diagnosis document does not try to land a tactical patch that cannot exist within the current ProtoList API contract.

The leverage on `list_append_loop` from steps #1, #3, #4 has already brought wall-clock down from 502 ms to 300 ms — a **−40 % win** without touching the immutable kernel contract. Further savings from this row require the kernel-level decision above.

## Concrete next step (if you ever pick this up)

Open a protoCore RFC titled "Mutable ProtoList backend" that:

1. Defines the ABI for `ProtoMutableList` (and possibly `ProtoMutableTuple`, `ProtoMutableString`).
2. States the conversion rules between mutable and immutable forms (e.g. an immutable read of a mutable list freezes it as a one-time snapshot, or a copy-on-read).
3. Specifies the GC root contract for in-place mutation.
4. Proposes a single Python opcode lowering pattern for `list.append` to motivate the design.

Then protoPython's `OP_LIST_APPEND` (line 5519 of `src/library/ExecutionEngine.cpp`) gains a fast path that uses the mutable backend when the receiver carries an `is_mutable` marker.

Until then: this row stays at −40 %, not −80 %.
