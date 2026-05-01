# SP-B — Attribute-Resolution Bugs (Cluster 2) Design

**Status:** CLOSED on 2026-04-30. All five symptoms closed (B1, B2, B3-via-SP-C, B4-via-SP-F, B5 NoneType, B5-reraise-via-SP-G); B3 was escalated to sub-project SP-C and closed by it — see `2026-04-30-sp-c-mappingproxy-semantics-design.md` (CLOSED on 2026-04-30); B4 was closed by SP-F; the deferred B5-reraise portion was closed by SP-G.

**Closure summary:**
- B5 (NoneType portion) closed: commits `167697dd` + `aaafcc50` + `a5645ddf` (f-string conversion specifier compiler emit fix).
- B1 closed: commits `fe896c33` + `033d31a1` + `7257e7e4` (`getType` honors __class__ only when own; generalizes V88 carve-out). Bonus: metaclass synthetic suite 34/2/1 → 35/2/0.
- B2 closed: commits `d7f144ee` + `4e1e3474` (`super().__init__(args)` was a no-op stub; now forwards via py_super_getattr + callObjectEx with kwargs reconstructed from `getCurrentKwNames()`).
- B3 closed by SP-C: commits `ba1acb46` (C1) + `015b3a82` (C2) + `f3d7f61f` (C3, fixup `798873ab`). Three entangled root causes in MappingProxy / cls.__dict__ semantics resolved; verified by `tests/synthetic/sp_c_phase4_repro.py` 10/10.
- B5 (reraise portion) closed by SP-G: the active-exception stack was stored exclusively on `_active_excs` (a ProtoList attribute on the per-thread py_thread object). Reads went through protoCore's mutable-shard attribute cache, which can serve stale (null) results when the read happens in a different ProtoContext than the write — exactly the SP0-P2.5 desync that was already fixed for `_pending_exc` with the `s_pendingExc` TLS mirror, but `_active_excs` had no equivalent mirror. Fix: add `s_activeExcs` — a thread_local `std::vector<const ProtoObject*>` parallel to `s_pendingExc`. Reads now use the mirror exclusively; the attribute storage write is preserved so GC reachability (py_thread is rooted via `s_globalThreadRootsDict`) is unchanged. Reproducer `tests/synthetic/sp_g_b5_reraise_repro.py` passes 10/10 (was: `import base64; import test.support` failed with `RuntimeError: reraise outside of except block` inside `importlib._bootstrap._find_and_load`'s with-block exception path).
- B4 closed by SP-F: `py_property_set` (`src/library/BuiltinsModule.cpp:4128-4180`) was passing the sentence fragment "property has no setter" as the `attr` argument to `raiseAttributeError`, which then formatted it as `'<TypeName>' object has no attribute 'property has no setter'` and copied that fragment into the AttributeError's `name` slot — surfacing as the malformed shape `'socket' has no attribute 'property has no setter'` in the SP-B audit. Fix: added `raiseAttributeErrorWithMessage` helper that decouples the human-readable message from the machine-readable `name`; `py_property_set` now resolves the property name from `fget.__name__`, the class name from `getType(obj).__name__`, and emits CPython 3.13+-shape `property '<name>' of '<TypeName>' object has no setter` while storing the actual property identifier in `name`. Reproducer `tests/synthetic/sp_f_b4_repro.py` passes 10/10. test_sys still CRASH but the underlying error now reads correctly (`property 'family' of 'socket' object has no setter` → reveals a separate read-only-property assignment issue in `socket.__init__`, out of B4 scope).
- B-DD1 closed by SP-E: commit `d0e26704` (purely defensive — the original "filter PROTO_NONE here to mirror the fast path" diagnosis was incorrect, because `ProtoObject::getAttribute` and `ProtoSparseList::getAt` use *different* absent-sentinel conventions; a naive PROTO_NONE filter would break `class C: y = None; print(y)` at class-body scope.  Fix uses `hasAttribute()` to disambiguate present-vs-absent before reading, locking the invariant against a future protoCore change that could otherwise reintroduce the catalogued ambiguity).
- B-DD2 closed by SP-D: commit `1ced646b` (purely defensive — no Python-level user-visible regression observed; locks the invariant locally so it does not depend on every caller staying upstream of `getType` forever).

SP-B is fully closed.  SP-C, SP-D, SP-E, SP-F, and SP-G are closed.

**Original status:** Draft, pending user review
**Author:** brainstorming session, 2026-04-30
**Project:** protoPython
**Sub-project:** SP-B — fix the 5 attribute-resolution-symptom crashes identified by the 2026-04-30 ground-truth audit

## Goal

Eliminate the five attribute-resolution-symptom crashes catalogued in `docs/superpowers/specs/2026-04-30-protopy-ground-truth-audit.md` (Cluster 2). After SP-B closes, the affected tests no longer fail at attribute-protocol level; they may still fail for cluster-1 reasons (stdlib import completeness), which is in scope for a separate SP-A.

## Non-goals

- Fixing cluster-1 stdlib import completeness (typing/doctest/asyncio/pdb/unittest.mock/test.support). That is SP-A's territory.
- Implementing missing language features. SP-B fixes existing protopy attribute-resolution behavior; it does not add new descriptor types or new metaclass features.
- Performance work. Correctness only.

## Cluster-2 universe (5 symptoms, from audit)

| # | Symptom | Affected tests | Hypothesized root area |
|---|---|---|---|
| B1 | `'ABCMeta' object has no attribute 'gen'` | test_asyncgen, test_contextlib | metaclass attribute lookup or `_GeneratorContextManagerBase()` returning the class itself |
| B2 | `'ArgumentParser' has no attribute 'conflict_handler'` | test_descr, test_re (via argparse import) | instance attribute initialization in `__init__`-equivalents |
| B3 | `'Point' object has no attribute 'x'` | test_dataclasses | descriptor `__set__` in dataclass-generated `__init__` not actually setting fields |
| B4 | `'socket' has no attribute 'property has no setter'` | test_sys | descriptor formatting bug — error message looks malformed (the attribute name contains a sentence fragment, which suggests the error string itself is being computed incorrectly) |
| B5 | `'NoneType' object is not callable` typing.py:20 + `reraise outside of except block` | test_base64, test_functools, test_grammar (cascade via typing import) | exception machinery (likely related to G silent-halt fix area) |

These are all real protopy bugs surfaced by the ground-truth audit (commit `a579440e`).

## Architecture — Iterative cascade

The audit-driven (option A) method: tackle one symptom, fix the root cause, re-run the audit, observe which other symptoms collapse with the fix. Repeat.

```
For each iteration N:
  1. Pick the next symptom (by priority — see Selection Policy below).
  2. Write minimal reproducer at tests/synthetic/sp_b_<symptom>_repro.py
     (5-15 lines, exits 0 on PASS, asserts on FAIL).
  3. Verify reproducer fails on current main.
  4. Diagnose root cause in protopy source (likely src/runtime/
     PythonEnvironment.cpp, ExecutionEngine.cpp, or descriptor-related
     paths in src/library/BuiltinsModule.cpp).
  5. Fix in C++ and/or Python.
  6. Build, verify reproducer passes.
  7. Re-run sp_audit_truth.py.  Note which OTHER symptoms also closed
     (likely shared root causes will collapse multiple symptoms at once).
  8. Regression check (synthetic suites at baseline, bootstrap PASS).
  9. Commit.  One commit per ROOT CAUSE — if it closes B1+B5 jointly,
     one commit covers both.
  10. Mark closed symptoms in this design doc's tracking table.
```

When all 5 symptoms are closed: SP-B done.

## Selection Policy — first iteration

Start with **B5** (`typing.py:20 NoneType callable` + `reraise outside of except`). Justification:

- **Highest cascade**: `typing` is imported transitively by test_base64, test_functools, test_grammar (3 of the 13 crashing Essential+Important tests). Closing B5 unblocks the most surface area.
- **Probable shared root with G**: the silent-halt fix (commit `efcfa7f3`) targeted a TLS/cache desync in the pending-exception path. B5's "reraise outside except" symptom hints at a sibling defect in the same machinery. Diagnosis effort can leverage the gdb / trace techniques already used for G.
- **Diagnostic momentum**: typing.py:20 is `from abc import abstractmethod, ABCMeta`. A reproducer for B5 is a 2-line script (`from abc import ABCMeta` plus a marker print). Bisection is mechanical.

**If B5 turns out to be a 2+ week fix**, escalate before continuing — this is the SP0 lesson applied here.

## Subsequent iterations

After B5 closes, re-run the audit. The expected ordering of remaining symptoms (subject to revision based on what the audit shows after B5):

- **B1** (`ABCMeta.gen`): 2-test impact. If `_GeneratorContextManagerBase()` returning the class itself is the cause, that's a metaclass / `__call__` resolution bug — likely concentrated and tractable.
- **B3** (`Point.x`): 1-test impact, but the most isolated case (dataclass-generated `__init__` not setting fields). A 5-line reproducer pinpoints whether the bug is in `setattr`, in slot descriptor `__set__`, or in the `@dataclass` codegen.
- **B2** (`ArgumentParser.conflict_handler`): 2-test impact, but argparse is a deep dependency — the bug likely lies somewhere in the argparse `__init__` chain. Diagnose the failing assignment in argparse.py to localize.
- **B4** (`socket descriptor formatting`): 1-test impact, suspicious error string. Investigate whether this is a real lookup bug or a formatting bug in the error path.

The actual order after B5 will be re-evaluated based on which symptoms collapse with B5's fix.

## Per-symptom reproducer shape

Every symptom gets a reproducer at `tests/synthetic/sp_b_<symptom>_repro.py`. Shape (illustrative for B5):

```python
# tests/synthetic/sp_b_b5_typing_repro.py
"""SP-B / B5 reproducer — typing.py:20 NoneType callable.

The line `from abc import abstractmethod, ABCMeta` in typing.py
fails with 'NoneType' object is not callable.  This script triggers
just that import and asserts both names are bound.
"""
from abc import abstractmethod, ABCMeta

assert callable(abstractmethod), "abstractmethod is not callable"
assert isinstance(ABCMeta, type), f"ABCMeta is {type(ABCMeta).__name__}, expected type"

print("SP_B_B5_OK")
```

Apply the same pattern (≤15 lines, asserts on PASS, prints `SP_B_<id>_OK` marker) for B1-B4.

## Per-symptom commit policy

- **One commit per ROOT CAUSE**, not per symptom. If a fix closes B1 and B5 jointly, the commit message lists both.
- Commit message format: `<area>: fix SP-B/<symptom-list> — <one-line>`. For multi-symptom: `runtime: fix SP-B/B1+B5 — restore exception-machinery rebind in import path`.
- Each commit must include the reproducer file(s) for the symptom(s) it closes. Reproducers stay as permanent regression tests.

## Stop condition

SP-B ends when **all** are true:

- Each of B1..B5's audit entry shows `PASS_*` or `FAIL_UNITTEST` (i.e. no longer crashing on the cluster-2 symptom — may still fail for cluster-1 stdlib reasons).
- All 5 reproducers (`sp_b_b{1..5}_*_repro.py`) print their respective `SP_B_B{N}_OK` marker on every run, ≥30 consecutive runs.
- Synthetic generators+async suite, synthetic metaclass suite, bootstrap (`importlib`, `inspect`) and the SP0 phase reproducers (`sp0_phase{1,2,2_5}_repro.py`) all remain at baseline (no regressions).
- ctest 159/159.

No time-cap. Per the SP0 lesson, if any single iteration exceeds 5 days wall-clock, escalate.

## Phase-expansion policy

Same as SP0: if diagnosis of a symptom reveals close-sibling bugs in the same file/function/protocol, fix them in the same iteration. Do **NOT** expand to unrelated areas. If the iteration's scope balloons past 5 days or the bug count rises past 3 within one iteration, escalate per rule 3 (versioned planning).

## Tracking table (kept current through execution)

| # | Symptom | Status | Closed by commit | Notes |
|---|---|---|---|---|
| B1 | `ABCMeta.gen` missing | closed | fe896c33 | `getType` honored an inherited `__class__` from the parent class (which is the metaclass), so an instance of a class with metaclass=ABCMeta was reported as type ABCMeta. `runUserClassCall` then saw `isInstanceOfSelf=0`, skipped `__init__`, and `self.gen` was never set. Fix: only honor `__class__` when it is an *own* attribute of `obj`. Generalizes V88 from the `type` special case to every metaclass. Closes B1; advances `test_contextlib` to PASS and `test_asyncgen` past the ABCMeta.gen error. |
| B2 | `ArgumentParser.conflict_handler` missing | closed | d7f144ee | `super().__init__(args)` was a silent no-op: `py_super_init` was a stub that returned PROTO_NONE, registered as the proxy's `__init__` and intercepting the lookup before `__getattr__` could perform the MRO walk.  Fix: `py_super_init` now forwards via `py_super_getattr` (returns bound method) + `env->callObjectEx` (with kwargs reconstructed from `getCurrentKwNames()` since the sparse-list is hash-keyed). Closes B2; advances test_descr/test_re past the conflict_handler symptom (now blocked by frame-introspection bugs, separate cluster). |
| B3 | `Point.x` missing (dataclass) | closed (by SP-C) | ba1acb46 + 015b3a82 + f3d7f61f (fixup 798873ab) | Three entangled root causes resolved by SP-C C1/C2/C3: (1) the `in` operator on MappingProxy bypassed `__contains__` via a `__data__`/`asSparseList` fast path that probed the class's full attribute storage, so `'__init__' in cls.__dict__` was True for inherited `__init__`; (2) `py_mappingproxy_contains` had a `getItem` fallback that, on native classes, dispatched `__class_getitem__` and returned non-null `PROTO_NONE` (truthy), reporting any key as present and cascading through `enum.py`'s `_find_data_repr_` to break `import inspect`; (3) six remaining MP methods (`__iter__`, `keys`, `values`, `items`, `__getitem__`, `__len__`) plus `get` were not own-only, and sister `getItem` fallbacks in `__getitem__`/`get` had the same C2 false-positive. With C1+C2+C3 fixed, `'__init__' in cls.__dict__` is False on a fresh `@dataclass` class, dataclasses' `_set_new_attribute` proceeds to assign the synthesized `__init__`, and `Point(1, 2).x == 1`. Verified by `tests/synthetic/sp_c_phase4_repro.py` 10/10. Audit re-run: cluster-2 attribute-error symptom on test_dataclasses cleared (`'Point' object has no attribute 'x'` no longer surfaces); test_dataclasses remains CRASH on a separate residual sub-bug (synthesized-`__init__` default values not applied when corresponding positional argument omitted) which is out-of-scope for B3 and tracked separately. |
| B4 | `socket` descriptor formatting | closed (by SP-F) | (this commit) | `py_property_set` was passing the sentence fragment "property has no setter" as the `attr` argument to `raiseAttributeError`; the helper then formatted it as `'<TypeName>' object has no attribute 'property has no setter'` and copied the fragment into the AttributeError's `name` slot — exactly the malformed shape catalogued in the audit (`'socket' has no attribute 'property has no setter'`). Fix: added `raiseAttributeErrorWithMessage(ctx, obj, message, attr)` that decouples message text from `name` slot; `py_property_set` now resolves the property identifier from `fget.__name__`, the class name from `getType(obj).__name__`, and emits CPython 3.13+ shape `property '<name>' of '<TypeName>' object has no setter` while storing the property identifier (not the sentence fragment) in `name`. Reproducer `tests/synthetic/sp_f_b4_repro.py` passes 10/10. test_sys still CRASH but the underlying error now reads `property 'family' of 'socket' object has no setter` — exposing a separate read-only-property-assignment issue in `socket.__init__` that is out of B4 scope and tracked separately. |
| B5 (NoneType portion) | `typing.py:20 NoneType` callable | closed | 167697dd | f-string conversion (`!r`/`!s`/`!a`) inside a function emitted OP_LOAD_NAME for repr/str/ascii; in function scope LOAD_NAME could surface a stray PROTO_NONE before env->resolve fallback.  Routed through emitNameOp so the load picks LOAD_GLOBAL inside functions. |
| B5 (reraise portion) | `reraise outside of except block` | open | — | Confirmed sibling, *not* the same root cause as the NoneType portion: reproducible without any f-string after `import base64; from test.support import …`.  Tracked separately. |
| B-DD1 | OP_LOAD_NAME closure-chain branch (ExecutionEngine.cpp:3086) — sentinel-convention mismatch with sibling fast path | closed (by SP-E) | d0e26704 | Original catalog framing ("filter PROTO_NONE in this branch to mirror the fast-path filter") was based on a wrong assumption: `ProtoObject::getAttribute` (used in this branch) returns `nullptr` for not-found and the actual stored value for found, while `ProtoSparseList::getAt` (used in the fast path) uses `PROTO_NONE` as its absent-sentinel.  The two sibling APIs disagree on the convention, so the proposed PROTO_NONE filter would break `class C: y = None; print(y)` at class-body scope (where PROTO_NONE is the legitimate stored value, not the absent-sentinel).  Verified by writing the patch and reproducing under it (reverted; does not ship).  SP-E fix instead uses `hasAttribute(ctx, nameS) == PROTO_TRUE` to disambiguate present-vs-absent before calling `getAttribute`.  Costs one extra parent-chain walk per LOAD_NAME miss-on-fast-path, in exchange for explicit semantics that no longer silently rely on the two APIs sharing a convention they do not.  Note: closures themselves use LOAD_DEREF (different opcode); this branch fires only at module and class-body scope.  Reproducer `tests/synthetic/sp_e_b_dd1_repro.py` passes 10/10 — and also passes against the pre-fix binary, so this landed as a *purely defensive* fix.  Benchmark drift: geomean 3.79× → 3.81× (run-to-run noise); per-benchmark drift within 5-run noise band (`attr_lookup` 1.61× → 1.64×, `startup_empty` 0.66× → 0.67×). |
| B-DD2 | 5 sites in PythonEnvironment.cpp still walk parent chain for `__class__` instead of routing through getType — same metaclass-leak shape as B1 | closed (by SP-D + 5th-site follow-up) | 1ced646b + follow-up | Five sites now call `getType(context, obj)` instead of `obj->getAttribute(ctx, "__class__")`. Sites: `py_type_instancecheck` (~2788, MRO walk), `handleException` (~11845, sys.last_type), `formatException` (~11913, type name in error message), eval/exec wrapper (~12196, SyntaxError detection), and `__getattribute__` AttributeError fallback (~12682, exception class name filter — added in code-review follow-up). The 5th site was missed in the original audit; recursion safety verified — `getType` reads `__class__` via the unscoped `proto::ProtoObject::getAttribute`, not `this->getAttribute`, so re-entry into `PythonEnvironment::getAttribute` from inside the `__getattribute__` MRO walk is bounded. Reproducer `tests/synthetic/sp_d_b_dd2_repro.py` passes 10/10 — and also passes against the pre-fix binary, so this landed as a *purely defensive* fix. Most upstream callers (`type()`, except-clause matching, OP_LOAD_NAME) already route through `getType`, and every instance allocated via `py_object_new` carries its own `__class__` (so the parent-chain walk used pre-fix would have terminated on the OWN attribute anyway). The fix locks the invariant locally rather than relying on every caller staying upstream of `getType` forever. Reproducer docstring updated to honestly describe what is fenced (upstream invariant, not the routed sites themselves). |

The implementer updates this table on every commit.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| B5 turns out to be a multi-week project (deep exception machinery) | 5-day escalation guard; if exceeded, defer B5 to a dedicated sub-project |
| Fixing one symptom regresses another | Step 7 + Step 8 of the cycle; full audit + regression check before commit |
| Audit reveals additional cluster-2 symptoms after a fix | Add to the tracking table and continue |
| A "fix" turns out to mask a deeper bug | Reproducers stay as permanent regression tests; audit is rerunnable |
| Cluster boundaries are wrong (a "B" symptom is actually cluster-1) | Reclassify and route to SP-A; document in the tracking table |

## Bridge back

When SP-B closes, the 5 cluster-2 tests no longer crash at attribute-resolution. They may still fail for cluster-1 stdlib reasons (they will — the audit shows test_dataclasses needs `unittest.mock`, etc.). SP-A (cluster 1) becomes the natural next sub-project to unblock the same tests at a different layer.

A small "SP-B closure" commit:

- Updates the audit document with the new symptom statuses.
- Updates this design's tracking table to mark all 5 symptoms closed.
- Adds a V155.1 (or next-available) entry to `CPYTHON_CONFORMANCE.md` reflecting the cluster-2 closure.

## Deliverables

| Artifact | Persists? |
|---|---|
| `tests/synthetic/sp_b_b{1..5}_*_repro.py` (5 reproducers) | Yes — permanent regression tests |
| Fixes in `src/runtime/*.cpp`, `src/library/*.cpp`, `lib/python3.14/*.py` per root cause | Yes |
| ≥1 commit per root cause (≥1, ≤5 commits expected) | Yes |
| Updated tracking table in this spec doc | Yes |
| New entry in `docs/CPYTHON_CONFORMANCE.md` reflecting SP-B closure | Yes |
| Re-run audit (`python3 tests/synthetic/sp_audit_truth.py`) showing cluster-2 cleared | Yes (rerun on demand) |

## Next step

User reviews this spec. After approval, the brainstorming flow invokes `superpowers:writing-plans` to produce the executable implementation plan.
