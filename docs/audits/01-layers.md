# P1 — ABI layer table

## Convention

protoCore has two parallel hierarchies for every Cell-allocated type:

- **API class** (e.g. `ProtoSparseList`) — declared in `headers/protoCore.h`, never instantiated. Used as a *typed handle* whose pointer carries a 6-bit `pointer_tag` in its low bits. Public methods on the API class are *trampolines*: they accept `this` as a tagged pointer, do `toImpl<...>` to reach the implementation, dispatch, and convert the result back to a tagged pointer when returning.

- **IMPL class** (e.g. `ProtoSparseListImplementation`) — declared in `headers/proto_internal.h`, derives from `Cell`, holds the actual fields. Pointers to IMPL classes are *raw aligned addresses* (low 6 bits zero by 64-byte cell alignment). They are private to protoCore: never exposed to user code.

The *invariant* that ties the two together:

> Public-API tagged pointer → `toImpl<X>(p)` → raw IMPL pointer.
> Raw IMPL pointer → `impl->asObject(ctx)` (or `asXyz(ctx)`) → public-API tagged pointer.

> Internal struct fields (private to protoCore) **must** hold raw IMPL pointers, never tagged-API handles. Internal recursion uses direct C++ pointer dereferencing, no `toImpl`, no tag dispatch.

The recent `oc->attributes` bug (#92) was exactly a violation of this invariant: the field was declared as the API tagged type, accessed as both API (via trampolines) and quasi-IMPL (via `sparseListGetRaw`). Defensive guards (`(uintptr_t)node & 0x3F != 0`) hid the inconsistency at runtime.

## Canonical layer table

Every Cell-derived class with its API counterpart, tag, and conversion methods.

| IMPL class | API class | POINTER_TAG_* | API ↔ IMPL conversion | Has Small variant |
|---|---|---|---|---|
| `ProtoObjectCell` | `ProtoObject` | `OBJECT` (0) | `oc->asObject(ctx)` ↔ `toImpl<ProtoObjectCell>(o)` | — |
| `ParentLinkImplementation` | (none, internal) | `OBJECT` (0) — shares tag with ProtoObjectCell ⚠️ | `pl->getObject(ctx)` returns the linked object | — |
| `ProtoListImplementation` | `ProtoList` | `LIST` (2) | `impl->asProtoList(ctx)` ↔ `toImpl<ProtoListImplementation>(l)` | `ProtoListSmallImplementation` (tag 25) |
| `ProtoListSmallImplementation` | `ProtoList` (same API) | `LIST_SMALL` (25) | `small->asProtoList(ctx)` ↔ `toImpl<ProtoListSmallImplementation>(l)` | (is the small) |
| `ProtoListIteratorImplementation` | `ProtoListIterator` | `LIST_ITERATOR` (3) | `impl->implAsObject(ctx)` ↔ `toImpl<ProtoListIteratorImplementation>(...)` | — |
| `ProtoSparseListImplementation` | `ProtoSparseList` | `SPARSE_LIST` (8) | `impl->asSparseList(ctx)` ↔ `toImpl<ProtoSparseListImplementation>(s)` | `ProtoSparseListSmallImplementation` (tag 26) |
| `ProtoSparseListSmallImplementation` | `ProtoSparseList` (same API) | `SPARSE_LIST_SMALL` (26) | `small->asSparseList(ctx)` ↔ `toImpl<ProtoSparseListSmallImplementation>(s)` | (is the small) |
| `ProtoSparseListIteratorImplementation` | `ProtoSparseListIterator` | `SPARSE_LIST_ITERATOR` (9) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | — |
| `ProtoTupleImplementation` | `ProtoTuple` | `TUPLE` (4) | `impl->asProtoTuple(ctx)` ↔ `toImpl<ProtoTupleImplementation>(t)` | — |
| `ProtoTupleIteratorImplementation` | `ProtoTupleIterator` | `TUPLE_ITERATOR` (5) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | — |
| `ProtoStringImplementation` | `ProtoString` | `STRING` (6) **and** `SYMBOL` (22) — same impl class, two tags ⚠️ | `impl->implAsObject(ctx)` (sets tag based on `is_strong`) | `StringLeafNode` (tag 23), `StringInternalNode` (tag 24) — internal AVL nodes, NOT a "small" variant |
| `ProtoStringIteratorImplementation` | `ProtoStringIterator` | `STRING_ITERATOR` (7) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | — |
| `StringLeafNode` | (none, AVL internal) | `STRING_LEAF_NODE` (23) | `toImpl<StringLeafNode>(p)` only | (is a node) |
| `StringInternalNode` | (none, AVL internal) | `STRING_INTERNAL_NODE` (24) | `toImpl<StringInternalNode>(p)` only | (is a node) |
| `ProtoSetImplementation` | `ProtoSet` | `SET` (16) | `impl->asProtoSet(ctx)` ↔ `toImpl<...>` | — |
| `ProtoSetIteratorImplementation` | `ProtoSetIterator` | `SET_ITERATOR` (18) | `impl->asObject(ctx)` ↔ `toImpl<...>` | — |
| `ProtoMultisetImplementation` | `ProtoMultiset` | `MULTISET` (17) | `impl->asProtoMultiset(ctx)` ↔ `toImpl<...>` | — |
| `ProtoMultisetIteratorImplementation` | `ProtoMultisetIterator` | `MULTISET_ITERATOR` (19) | `impl->asObject(ctx)` ↔ `toImpl<...>` | — |
| `ProtoByteBufferImplementation` | `ProtoByteBuffer` | `BYTE_BUFFER` (10) | `impl->asByteBuffer(ctx)` ↔ `toImpl<...>` | — |
| `ProtoExternalPointerImplementation` | `ProtoExternalPointer` | `EXTERNAL_POINTER` (11) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | — |
| `ProtoExternalBufferImplementation` | `ProtoExternalBuffer` | `EXTERNAL_BUFFER` (20) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | — |
| `ProtoMethodCell` | (none, accessed via `ProtoObject::asMethod`) | `METHOD` (12) | `cell->implAsObject(ctx)` ↔ `toImpl<ProtoMethodCell>(m)` | — |
| `ProtoThreadImplementation` | `ProtoThread` | `THREAD` (13) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | — |
| `ProtoThreadExtension` | (none, internal) | `OBJECT` (0) — shares with ProtoObjectCell ⚠️ | `toImpl<ProtoThreadExtension>(p)` | — |
| `LargeIntegerImplementation` | (none — exposed via tagged primitive system) | `LARGE_INTEGER` (14) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | inline `SmallInteger` via `EMBEDDED_VALUE` (1) |
| `DoubleImplementation` | (none) | `DOUBLE` (15) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | — |
| `ProtoRangeIteratorImplementation` | (none — read via `ProtoObject::isNativeRangeIterator` etc.) | `RANGE_ITERATOR` (21) | `impl->implAsObject(ctx)` ↔ `toImpl<...>` | — |
| `ReturnReference` | (none, internal) | `OBJECT` (0) — shares with ProtoObjectCell ⚠️ | n/a | — |
| `TupleDictionary` | (none, internal) | `OBJECT` (0) — shares with ProtoObjectCell ⚠️ | n/a | — |

### Tag-0 collision

Five distinct cell classes share `POINTER_TAG_OBJECT` (0):

- `Cell` (the abstract base — shouldn't appear at runtime)
- `ProtoObjectCell` (the typical user-visible object)
- `ParentLinkImplementation` (the prototype-chain link node)
- `ProtoThreadExtension` (per-thread auxiliary state)
- `ReturnReference` (internal control-flow marker)
- `TupleDictionary` (internal lookup helper)

Distinguishing them at runtime requires a virtual `getType()` call (see `isObjectFast` in `proto_internal.h:602`). The pure tag-only check `(uintptr_t)p & 0x3F == 0` is **not** safe to assume "this is a ProtoObjectCell"; it only narrows down to "one of these six".

This collision is real and intentional (saves a tag) but it is a frequent source of subtle bugs (#92's chain walker assumed tag-0 ⇒ ProtoObjectCell). Every chain walk and `toImpl<ProtoObjectCell>` site must either:

1. Be reachable only from contexts where the tag-0 cell is *guaranteed* by construction to be a ProtoObjectCell (and the assumption documented).
2. Use `isObjectFast(p)` to disambiguate.

Currently the discipline is mixed.

## Audit of internal struct fields

For every IMPL class, every field that holds a Cell-derived pointer was inspected. The expected pattern is *raw IMPL*; *API tagged* in private fields is the anti-pattern that surfaced as #92.

| Class | Field | Declared type | Layer | Status |
|---|---|---|---|---|
| `ProtoObjectCell` | `parent` | `const ParentLinkImplementation*` | IMPL raw | ✅ correct |
| `ProtoObjectCell` | `attributes` | `const ProtoSparseListImplementation*` | IMPL raw | ✅ **fixed in the current session** (was API tagged → #92) |
| `ParentLinkImplementation` | `parent` | `const ParentLinkImplementation*` | IMPL raw | ✅ correct |
| `ParentLinkImplementation` | `object` | `const ProtoObject*` | API tagged | ✅ correct (the linked object can be any tagged value, not just a cell) |
| `ProtoListImplementation` | `value` | `const ProtoObject*` | API tagged | ✅ correct (stores arbitrary user values) |
| `ProtoListImplementation` | `previousNode`, `nextNode` | `const ProtoListImplementation*` | IMPL raw | ✅ correct |
| `ProtoListSmallImplementation` | `slots[5]` | `const ProtoObject*` | API tagged | ✅ correct (user values) |
| `ProtoSparseListImplementation` | `value` | `const ProtoObject*` | API tagged | ✅ correct |
| `ProtoSparseListImplementation` | `previous`, `next` | `const ProtoSparseListImplementation*` | IMPL raw | ✅ correct (post-fix) |
| `ProtoSparseListSmallImplementation` | `keys[3]` | `unsigned long` | scalar | ✅ |
| `ProtoSparseListSmallImplementation` | `values[3]` | `const ProtoObject*` | API tagged | ✅ correct |
| `ProtoSparseListIteratorImplementation` | `current` | `const ProtoSparseListImplementation*` | IMPL raw | ✅ correct |
| `ProtoSparseListIteratorImplementation` | `queue` | `const ProtoSparseListIteratorImplementation*` | IMPL raw | ✅ correct |
| `ProtoTupleImplementation` | `slot[TUPLE_SIZE]` | `const ProtoObject*` | API tagged | ✅ correct (user values) |
| `ProtoTupleIteratorImplementation` | `base` | `const ProtoTupleImplementation*` | IMPL raw | ✅ correct |
| `ProtoSetImplementation` | `list` | `const ProtoSparseList*` | **API tagged** | ⚠️ **VIOLATION** — should be `const ProtoSparseListImplementation*` |
| `ProtoMultisetImplementation` | `list` | `const ProtoSparseList*` | **API tagged** | ⚠️ **VIOLATION** — should be `const ProtoSparseListImplementation*` |
| `ProtoSetIteratorImplementation` | `iterator` | `const ProtoSparseListIteratorImplementation*` | IMPL raw | ✅ correct |
| `ProtoMultisetIteratorImplementation` | `iterator` | `const ProtoSparseListIteratorImplementation*` | IMPL raw | ✅ correct |
| `ProtoStringImplementation` | `avl_root` | `const ProtoObject*` | **API tagged**, but used as a tag-discriminated union (`StringLeafNode` / `StringInternalNode`) | ⚠️ **DESIGN AMBIGUITY** — the field stores either tag 23 (LEAF) or tag 24 (INTERNAL); typing as `const ProtoObject*` is the existing convention for tag-discriminated internal unions but loses static type safety. Document or refactor to a typed sum. |
| `StringInternalNode` | `left`, `right` | `const ProtoObject*` | API tagged (same union) | ⚠️ same pattern as `avl_root` |
| `ProtoMethodCell` | `self` | `const ProtoObject*` | API tagged | ✅ correct (the bound receiver can be any value) |
| `ProtoThreadImplementation` | `args` | `const ProtoList*` | **API tagged** | ⚠️ **VIOLATION** — should be `const ProtoListImplementation*` |
| `ProtoThreadImplementation` | `kwargs` | `const ProtoSparseList*` | **API tagged** | ⚠️ **VIOLATION** — should be `const ProtoSparseListImplementation*` |
| `ProtoThreadImplementation` | `name` | `const ProtoString*` | **API tagged** | ⚠️ **VIOLATION** — should be `const ProtoStringImplementation*` |
| `ProtoThreadImplementation` | `extension` | `ProtoThreadExtension*` | IMPL raw | ✅ correct |
| `ProtoThreadImplementation` | `space` | `ProtoSpace*` | (special) | ✅ correct |

## Findings

### F1.1 — Anti-pattern recurs in 5 IMPL classes

The same anti-pattern that caused #92 (private struct field declared as API tagged pointer) is still present in:

- `ProtoSetImplementation::list` — `ProtoSparseList*` should be `ProtoSparseListImplementation*`.
- `ProtoMultisetImplementation::list` — same.
- `ProtoThreadImplementation::args` — `ProtoList*` should be `ProtoListImplementation*`.
- `ProtoThreadImplementation::kwargs` — `ProtoSparseList*` should be `ProtoSparseListImplementation*`.
- `ProtoThreadImplementation::name` — `ProtoString*` should be `ProtoStringImplementation*`.

**Severity: HIGH.** Same root-cause class as #92. Set/Multiset use tag-dispatched trampolines internally so the bug may already be latent there too. ProtoThreadImplementation holds the args/kwargs of the call frame — corrupted by GC under pressure with the same mechanism as #92.

**Severity-aware nuance**: ProtoSet/Multiset operate on the public `ProtoSparseList::*` API which dispatches by tag. So tagging the field is "consistent" with how it's used. But it is still inconsistent with the layer rule, and any change to the SparseList Small/AVL choice rebalances the API.

**Action**: open three follow-up issues to retype these fields, mirroring the recent `attributes` fix.

### F1.2 — `avl_root` typed as `ProtoObject*` (tag-discriminated union)

`ProtoStringImplementation::avl_root` and `StringInternalNode::left`/`right` are `const ProtoObject*`. The actual values stored are always one of:

- `nullptr` (empty)
- A `StringLeafNode*` with tag 23
- A `StringInternalNode*` with tag 24

The choice to use `const ProtoObject*` is reasonable when a field is genuinely polymorphic across multiple tag spaces. Here the polymorphism is binary (LEAF vs INTERNAL). The current code dispatches via `pa.op.pointer_tag` checks every read.

**Severity: MEDIUM.** Not a UAF risk — the AVL is built and read by code that knows the discipline. But typing as `ProtoObject*` loses the static check that "only LEAF/INTERNAL pointers go here" and forces every reader to perform tag dispatch.

**Action**: candidate refactor to introduce a sum type or `union { StringLeafNode*; StringInternalNode*; }` plus a discriminator. Lower priority than F1.1.

### F1.3 — Tag-0 collision is undocumented as a risk

Five Cell-derived classes share tag 0. The chain walker in `ProtoObject::getAttribute` assumes tag-0 implies `ProtoObjectCell`. The discipline that enforces this is buried in newChild/setAttribute/etc. and is *not* documented as a contract.

**Severity: MEDIUM.** Has been the cause of two bugs already (#92 was partially this; the comment at `proto_internal.h:580` acknowledges the risk).

**Action**: document the tag-0 invariant in a header comment, and audit every `toImpl<ProtoObjectCell>` site for whether the precondition is provable from the call chain.

### F1.4 — `isObjectFast` exists but is not used at all `toImpl<ProtoObjectCell>` call sites

`headers/proto_internal.h:602` defines `isObjectFast(p)` which combines tag check + virtual `getType()` call to reliably distinguish `ProtoObjectCell` from `ParentLinkImplementation`/`ProtoThreadExtension`/`ReturnReference`/`TupleDictionary`.

It's used in `setAttribute`, `removeAttribute`, `getFirstParent` etc. — but **NOT** at the chain walker in `getAttribute`/`hasAttribute`/`getOwnAttributes`/etc. There the bare tag check is used for performance, with the assumption that "the chain only visits ProtoObjectCells". That assumption is what failed under #92.

**Severity: HIGH.** This is effectively a defensive-coding inconsistency. Either every `toImpl<ProtoObjectCell>` should use `isObjectFast` (paying one virtual call per chain step), or the invariant must be air-tight by construction (no leak path). The current state is "mostly safe" which is exactly what produces hard-to-debug crashes.

**Action**: decide one or the other. Either pay the virtual call, or formalise the construction-time invariant in a doc + invariant-checked construction helpers.

## Summary

The layering convention is real and mostly observed, but **not formalised**. Five IMPL classes still hold API tagged pointers in private fields (F1.1). The tag-0 collision is undocumented as a hazard (F1.3), and the helper that exists to guard against it (`isObjectFast`) is used inconsistently (F1.4). The `avl_root` polymorphic field (F1.2) is a smaller hygiene issue.

Top action items, ranked:

1. **F1.1**: retype 5 fields. Mechanical change, high payoff. (Should match the work pattern of the `attributes` fix from this session.)
2. **F1.4**: pick one discipline for `toImpl<ProtoObjectCell>` and apply it uniformly.
3. **F1.3**: document the tag-0 invariant.
4. **F1.2**: refactor `avl_root` to a typed sum (lower priority).

Estimated total effort: 2-4 hours for F1.1, 4-8 hours for F1.4 (depends on benchmarking the virtual call cost), F1.3 is one comment block, F1.2 is hygiene.
