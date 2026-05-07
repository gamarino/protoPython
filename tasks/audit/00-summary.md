# Audit summary and remediation roadmap

**Date**: 2026-05-07.
**Sources**: `00-plan.md`, `01-layers.md`, `02-fast-paths.md`, `03-gc-roots.md`, `04-native-stubs.md`.

## Headline

The audit produced **24 distinct findings across 4 axes**, organised below. Most map to **3 root causes** that recur across the codebase:

1. **Type discipline at API/IMPL boundary is informal**. Private struct fields hold tagged API pointers; trampolines and internal recursion mix layers; defensive guards mask the inconsistency. Cause of #92, F1.1, F1.2, F1.4.

2. **Fast paths gate on "is it a built-in" instead of "is it exactly the built-in"**. Subclasses sneak through and bypass overrides. Cause of #89, #90, F2.1, F2.2, F2.4.

3. **No formal policy for GC root coverage of `ProtoObject*` in C++ locals**. Some sites pin via operand stack (works), some via `ProtoRootSet` (works after this session's fix), most rely on coincidence. Cause of #92's hang, F3.1, F3.4, F3.5.

A 4th axis (native module stubs vs CPython) compounds the others by hiding bugs behind silent success.

## Combined finding table

Sorted by severity × frequency. "Pattern" indicates which root cause family the finding belongs to.

| ID | Title | Severity | Pattern | Sites | Effort |
|---|---|---|---|---|---|
| F2.1 | 7 binary arithmetic ops bypass dunder for subclasses | HIGH | fast-path | 7 | 1-2h |
| F4.1 | `CollectionsAbcModule` ABC methods 15+ no-ops | HIGH | stub | 15+ | 4-8h |
| F4.2 | `HeapqModule` is broken; `BisectModule` incomplete | HIGH | stub | 2 modules | 3h (replace) |
| F4.3 | `SignalModule` doesn't deliver signals | HIGH | stub | 1 | 4h |
| F4.4 | `IOModule` file objects can't iterate | HIGH | stub | 1 | 3-4h |
| F1.1 | 5 IMPL classes hold API tagged in private fields | HIGH | layer | 5 | 2-4h |
| F3.1 | 6+ native iteration trampolines unpinned | HIGH | gc-root | 6+ | 3-4h |
| F3.4 | 8+ `env->callObject` callsites unpinned | HIGH | gc-root | 8+ | 1-2h |
| F2.2 | `OP_BINARY_SUBSCR`/`OP_STORE_SUBSCR` bypass list subclass | HIGH | fast-path | 2 | 30min |
| F1.4 | `isObjectFast` not used at all `toImpl<ProtoObjectCell>` sites | HIGH | layer | many | 4-8h |
| F2.5 | dunder lookup walks proto-chain not `__mro__` | MEDIUM-HIGH | fast-path | TBD | 1-2h to audit |
| F4.5 | Thread/Math/Os modules have many PROTO_NONE stubs | MEDIUM-HIGH | stub | 3 modules | 8-12h |
| F2.3 | Subclass `__rop__` priority not implemented | MEDIUM | fast-path | 1 dispatcher | 1h |
| F2.4 | `OP_CONTAINS` priority dispatch excludes built-in subclasses | MEDIUM | fast-path | 1 | 30min |
| F3.5 | Bytecode opcode handlers with internal iterators unpinned | MEDIUM | gc-root | 4+ | 1h |
| F1.3 | tag-0 collision undocumented as a hazard | MEDIUM | layer | 1 doc | 30min |
| F4.6 | Many secondary modules unaudited | TBD | stub | ~10 modules | 4h audit |
| F3.6 | `invokeCallable` pin only fires for asMethod path | LOW-MEDIUM | gc-root | 1 | 30min |
| F3.2 | No reusable `TransientPin` RAII helper | MEDIUM (architectural) | gc-root | 1 helper | 30min |
| F2.6 | Truthiness/length fast paths unaudited | TBD | fast-path | TBD | 30min audit |
| F1.2 | `avl_root` typed as `ProtoObject*` (tag-discriminated union) | MEDIUM | layer | 1 | 1-2h refactor |

## Recommended order of attack

The cheapest, highest-leverage wins, ordered by ratio of (severity × likelihood) / effort:

### Sprint 1 — quick architectural wins (1 day total)

These are mostly mechanical, with templates already established this session:

1. **F3.2** — introduce `TransientPin` RAII helper. *30 min.* Unblocks F3.1 / F3.4 / F3.5 work.
2. **F2.1** — apply `binaryAdd`-style guard to the other 7 arithmetic ops. *1-2h.*
3. **F2.2** — gate `OP_BINARY_SUBSCR` / `OP_STORE_SUBSCR` list fast paths with exact-type check. *30 min.*
4. **F2.4** — extend `OP_CONTAINS` priority dispatch to fire for built-in container subclasses. *30 min.*
5. **F2.3** — implement subclass `__rop__` priority in `binaryOpDispatch`. *1h.*
6. **F1.1** — retype 5 IMPL fields (Set/Multiset/Thread args/kwargs/name). *2-4h.*

**Total Sprint 1: ~6-8 hours.** Eliminates the 7-fold recurrence of #89's bug, the 5 remaining instances of #92's anti-pattern, and the missing CPython subclass priority rule.

### Sprint 2 — GC discipline cleanup (1 day)

7. **F3.1** — pin loops in `functools.reduce`, `py_collections_*`, `py_mapping_*`, `py_*_repr` using the helper from F3.2. *3-4h.*
8. **F3.4** — pin `env->callObject` callsites. *1-2h.*
9. **F3.5** — pin internal iterators in opcode handlers. *1h.*
10. **F3.6** — extend args pin to non-asMethod `invokeCallable` paths. *30 min.*
11. Document the discipline in `protoPython/CLAUDE.md`. *30 min.*

**Total Sprint 2: ~6-8 hours.** Eliminates the deep-recursion UAF class entirely.

### Sprint 3 — semantic-discipline cleanup (1 day)

12. **F2.5** — audit and fix dunder lookup sites that walk proto-chain instead of `__mro__`. *1-2h.*
13. **F2.6** — audit `isTruthy` and length fast paths. *30 min.*
14. **F1.3** — document tag-0 invariant. *30 min.*
15. **F1.4** — pick one discipline for `toImpl<ProtoObjectCell>` (with-isObjectFast vs by-construction) and apply uniformly. *4-8h.*
16. **F1.2** — refactor `avl_root` to a typed sum (lower priority). *1-2h.*

**Total Sprint 3: ~7-13 hours.**

### Sprint 4 — module-level cleanup (1-2 weeks)

17. **F4.1** — finish `CollectionsAbcModule`. *4-8h.*
18. **F4.2** — replace `HeapqModule` and `BisectModule` with Python equivalents. *3h.*
19. **F4.3** — fix or remove `SignalModule`. *4h or 30min.*
20. **F4.4** — complete `IOModule` file methods. *3-4h.*
21. **F4.5** — audit-and-fix Thread/Math/Os per-function. *8-12h.*
22. **F4.6** — smoke-test secondary modules. *4h.*
23. **Architectural** — convert HeapqModule/BisectModule/FunctoolsModule/ItertoolsModule/JsonModule to pure Python where possible. *15-25h.*

**Total Sprint 4: ~40-60 hours.**

## What this audit delivered

- **`tasks/audit/00-plan.md`** — scope, methodology, success criteria.
- **`tasks/audit/01-layers.md`** — exhaustive ABI table for every Cell-derived class. 4 findings.
- **`tasks/audit/02-fast-paths.md`** — every fast-path gate inventoried. 6 findings.
- **`tasks/audit/03-gc-roots.md`** — every native function analyzed for GC discipline. 6 findings.
- **`tasks/audit/04-native-stubs.md`** — every native module compared with CPython spec. 6 findings.
- **This summary** — 24 findings, prioritised remediation in 4 sprints.

## What this audit explicitly did NOT do

- It did not produce code fixes — those are separate sessions.
- It did not exhaustively check every native module method against CPython (sampled, with the heaviest stubs called out specifically).
- It did not benchmark performance impact of the recommended fixes.
- It did not validate the audit findings with adversarial test cases.

These are the natural next steps but are out of scope for the conceptual audit itself.

## Lessons captured for `tasks/lessons.md`

Three architectural rules emerged. They should be added to `tasks/lessons.md` as durable guidance:

### Rule 1 — Layer discipline

> Every `*Implementation` class in protoCore is internal. Its struct fields hold raw C++ pointers to other `*Implementation` classes (tag-0 aligned, low-bits-zero). The public-API `Proto*` classes are tagged-pointer phantom types reachable only via `asObject()`/`asXyz()` conversion methods. Trampolines on the public class do `toImpl<>` at the entry, dispatch internally with raw pointers, and `asObject()` at the exit.
>
> Private struct fields **must not** be declared as the public API tagged type. Any internal recursion **must** use direct C++ method calls (no toImpl, no tag dispatch).
>
> Defensive alignment guards (`(uintptr_t)p & 0x3F != 0`) in inner methods are a smell — they cover for ABI inconsistency upstream. Track them, then remove them by fixing the producer.

### Rule 2 — Fast-path discipline

> Every fast path in dispatch code that gates on a built-in type check (`isInteger`, `isString`, etc.) must verify **`type(x) == primitivePrototype` exactly**, not "is x of an integer kind". Otherwise Python subclasses with overridden dunder operators silently fall into the C primitive path.
>
> The standard guard helper:
> ```cpp
> bool aPrim = (env->getType(ctx, a) == env->getIntPrototype()
>               || env->getType(ctx, a) == env->getBoolPrototype()
>               || env->getType(ctx, a) == env->getFloatPrototype());
> ```
>
> CPython's subclass priority rule for reflected operators (try `b.__rop__` before `a.__op__` if `type(b)` is a proper subclass of `type(a)` overriding `__rop__`) must be honoured by every binary numeric/sequence dispatcher.

### Rule 3 — GC root discipline for native C functions

> Any `ProtoObject*` (or `Proto*` API) value held in a C++ local across a callback that may trigger GC (bytecode execution, dunder dispatch, attribute lookup, allocation) **must** be GC-rooted by one of:
>
> - Living on the operand stack of a `ProtoContext` for the duration of the callback.
> - Being a member of a Cell that's reachable from a GC root.
> - Pinned via `ProtoRootSet` (the `transientArgsRoots_` of `PythonEnvironment` is the standard one).
>
> The `TransientPin` RAII helper makes pinning a one-line operation. Use it whenever a native function holds a callable, an iterator, or any cell-derived value across `env->next` / `env->iter` / `env->callObject` / `invokeCallable`.

## Closing

The audit confirms the user's intuition was correct: the bugs we'd been hitting are **not random**. They cluster into three architectural patterns. Each pattern has a fix template, but until those templates were articulated, every recurrence had to be debugged from scratch — paying ~3-5 hours per incident (as we saw across this session).

Going forward:

- Sprint 1 (one day) eliminates the highest-velocity recurrences.
- Sprint 2 (one day) closes the GC discipline class entirely.
- Sprint 3 (one day) tightens the layer discipline.
- Sprint 4 is the longer module-level cleanup that runs in parallel.

After Sprint 1+2+3 (~3 days of focused work) the codebase will have **structural confidence** the user reported missing at the start of this audit. The remaining work in Sprint 4 is incremental, breadth-not-depth, and any single module's completion is independently shippable.
