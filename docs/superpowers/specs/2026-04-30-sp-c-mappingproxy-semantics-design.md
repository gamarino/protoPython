# SP-C — MappingProxy / cls.__dict__ Semantics (Design)

**Status:** Draft, pending user review
**Author:** brainstorming session, 2026-04-30
**Project:** protoPython
**Sub-project:** SP-C — fix the 3 entangled bugs that block SP-B/B3 (Point.x dataclass)

## Goal

Make `cls.__dict__` on protopy semantically equivalent to CPython: a live MappingProxy that exposes only the **own** attributes of the class, never inherited ones. This unblocks SP-B's B3 (dataclass `__init__` not setting fields) and any other downstream user of `cls.__dict__`.

## Background — how SP-C came to exist

SP-B's iterative cascade closed B5 (NoneType portion), B1 (ABCMeta.gen), and B2 (super.__init__ args). Diagnosis of B3 (`Point.x` missing on dataclass instance) revealed three entangled bugs that exceed the SP-B per-iteration guard:

1. **`in` operator on MappingProxy bypasses `__contains__`.** `compareOp` in `src/library/ExecutionEngine.cpp` (around line 1497) checks `b->getAttribute("__data__")` and uses an `asList`/`isTuple`/`asSparseList` fast path. For `cls.__dict__`, `__data__` IS the class itself; `data->asSparseList()` then probes the class's full attribute storage including inherited members.

2. **`py_type_get_dict` stores the class as `__data__`** (`src/library/PythonEnvironment.cpp:590-604`): `proxy->setAttribute(context, env->getDataString(), self)`. The proxy delegates lookups back to the class, which walks the parent chain.

3. **stdlib code depends on the broken behavior.** A targeted SP-B Task-4 attempt to route `in MP` through `__contains__` (which already filters own-only) broke `import inspect`. The exact stdlib site has not been identified; possible explanations include `cls.__dict__.keys()` returning inherited members, `dir(cls)` walking through MP methods we haven't yet filtered, or a legitimate use of an inherited attribute via a different (still broken) MP method.

Per rule 3 (versioned planning), B3 is escalated from SP-B to a dedicated SP-C. SP-B remains paused at 3/5 closed; SP-C closes B3 and reactivates B4 as future SP-B follow-up work.

## Non-goals

- Fixing B4 (socket descriptor formatting). Belongs to SP-B follow-up.
- Fixing other deferred SP-B items (B5-reraise, B-DD1, B-DD2). Independent work.
- Performance optimization. Correctness only.

## Architecture — Four iterative phases

```
SP-C: MappingProxy / cls.__dict__ Semantics
═══════════════════════════════════════════════════════════════════

Phase 1 — `in` operator routes MappingProxy through __contains__
   Goal: 'x' in cls.__dict__ uses __contains__'s own-only check.
   Files: src/library/ExecutionEngine.cpp (compareOp).
   Done when: tests/synthetic/sp_c_phase1_repro.py prints SP_C_PHASE1_OK
              for 10 consecutive runs.

Phase 2 — Diagnose and fix the inspect.py break
   Goal: import inspect succeeds after Phase 1's fix.
   Diagnosis-driven; fix lives wherever the root cause is
   (could be another MP method, or somewhere else entirely).
   Done when: tests/synthetic/sp_c_phase2_repro.py prints
              SP_C_PHASE2_OK for 10 consecutive runs.

Phase 3 — Audit + fix the 6 remaining MappingProxy methods
   Goal: __iter__, keys(), values(), items(), __getitem__, __len__
         all return own-only results.  __contains__ already covered
         in SP-B Task 4 attempt; verify still correct.
   Files: src/library/PythonEnvironment.cpp (mappingproxy method
          impls).
   Done when: tests/synthetic/sp_c_phase3_repro.py prints
              SP_C_PHASE3_OK for 10 consecutive runs.

Phase 4 — Verify B3 closure + audit re-run
   Goal: SP-B's B3 reproducer passes; ground-truth audit shows
         cluster-2 B3 cleared.
   Files: documentation only — update SP-B tracking, audit doc,
          and CPYTHON_CONFORMANCE.md V155.x entry.
   Done when: tests/synthetic/sp_c_phase4_repro.py + B3 reproducer
              both print their markers; audit re-run cluster-2 B3
              row no longer reports `Point.x` symptom.
```

Per-phase TDD cycle (mirrors SP-B):

1. Write the reproducer at the canonical path.
2. Verify reproducer fails on current main.
3. Diagnose root cause via reading source + `PROTO_ENV_DIAG=2` traces + gdb when silent.
4. Apply the fix.
5. Build (`cmake --build build 2>&1 | tail -5`).
6. Verify reproducer passes 10/10 runs.
7. Re-run `tests/synthetic/sp_audit_truth.py`; note any other symptoms that collapsed.
8. Regression check: ctest 159/159, synthetic generators 23/13/1, synthetic metaclass 35/2/0, all SP0 + SP-B reproducers green, custom Necessary suites at pre-fix state.
9. Commit per root cause (one commit may close more than one phase if root causes overlap).
10. Update tracking table in this design doc.

## Reproducers

### Phase 1 — `'in' MP` returns own-only

```python
# tests/synthetic/sp_c_phase1_repro.py
"""Phase 1 — `in cls.__dict__` returns own-only.

Pre-fix: 'x' (own) returned False, '__init__' (inherited) returned True
because the in-operator bypassed __contains__ and probed the class's
full attribute storage via asSparseList.
"""
class P:
    x = 1

d = P.__dict__
assert 'x' in d, "'x' should be in cls.__dict__ (own attr)"
assert '__init__' not in d, "'__init__' should NOT be in cls.__dict__ (inherited)"
assert '__class__' in d, "'__class__' should be in cls.__dict__ (own)"
assert 'nonexistent' not in d

print("SP_C_PHASE1_OK")
```

### Phase 2 — `import inspect` works

```python
# tests/synthetic/sp_c_phase2_repro.py
"""Phase 2 — import inspect succeeds after Phase 1.

If Phase 1 broke inspect (regression seen during SP-B Task 4 attempt),
Phase 2 fixes the underlying cause.
"""
import inspect

assert hasattr(inspect, "signature")
assert hasattr(inspect, "isclass")
print("SP_C_PHASE2_OK")
```

### Phase 3 — All 6 MP methods own-only

```python
# tests/synthetic/sp_c_phase3_repro.py
"""Phase 3 — All 6 MappingProxy methods return own-only data."""
class P:
    x = 1
    y = "hello"

d = P.__dict__

# 1. keys()
keys = list(d.keys())
assert 'x' in keys and 'y' in keys
assert '__init__' not in keys

# 2. values()
vals = list(d.values())
assert 1 in vals and "hello" in vals

# 3. items()
items = dict(d.items())
assert items.get('x') == 1
assert items.get('y') == "hello"
assert '__init__' not in items

# 4. __iter__
iter_keys = [k for k in d]
assert 'x' in iter_keys
assert '__init__' not in iter_keys

# 5. __getitem__
assert d['x'] == 1
try:
    _ = d['__init__']
    raise AssertionError("d['__init__'] should KeyError (inherited)")
except KeyError:
    pass

# 6. __len__ matches __iter__ count
n_own = sum(1 for _ in d)
assert len(d) == n_own, f"len(d)={len(d)} != iter count {n_own}"

print("SP_C_PHASE3_OK")
```

### Phase 4 — B3 closure + dataclass

```python
# tests/synthetic/sp_c_phase4_repro.py
"""Phase 4 — SP-B/B3 closes; dataclass __init__ assignment works."""
from dataclasses import dataclass

@dataclass
class Point:
    x: int
    y: int = 0

p = Point(1, 2)
assert p.x == 1 and p.y == 2

print("SP_C_PHASE4_OK")
```

## MappingProxy methods — audit checklist (Phase 3)

| Method | Suspected wrong? | Action |
|---|---|---|
| `__contains__` | Already routed through `hasOwnAttribute` in SP-B Task 4 attempt; verify still correct | Re-verify after Phase 1 lands |
| `__iter__` | Likely walks all attrs (including inherited) | Filter to `getOwnAttributes` keys only |
| `keys()` | Likely walks all attrs | Same as `__iter__` |
| `values()` | Likely walks all attrs | Same |
| `items()` | Likely walks all attrs | Same |
| `__getitem__` | Likely uses `getAttribute` (parent chain) | Use `getOwnAttribute` + raise KeyError if absent |
| `__len__` | Likely returns total count | Count `getOwnAttributes` only |

Audit procedure per method: `grep` for the method registration in `mappingProxyPrototype` setup, read its impl, check if `getAttribute` vs `hasOwnAttribute`/`getOwnAttribute`. If wrong, fix and add a sub-assertion to `sp_c_phase3_repro.py`.

## Stop condition

SP-C ends when **all** are true:

- Each of `sp_c_phase{1,2,3,4}_repro.py` prints its respective `SP_C_PHASE{N}_OK` marker for 10 consecutive runs.
- `import inspect` works (Phase 2 closed).
- `tests/synthetic/sp_b_b3_dataclass_init_repro.py` (from SP-B Task 4) passes 10/10.
- Synthetic generators+async suite at baseline (23/13/1).
- Synthetic metaclass suite at baseline (35/2/0 — preserves SP-B/B1's improvement).
- `ctest --test-dir build`: 159/159.
- All SP0 reproducers (`sp0_phase{1,2,2_5}_repro.py`) green.
- All SP-B reproducers (`sp_b_b{5,1,2}_*_repro.py`) green.
- Custom Necessary suites at pre-fix state (test_contextlib still PASS, etc.).
- Ground-truth audit re-run: cluster-2 B3 row no longer reports `'Point' object has no attribute 'x'`.

No time-cap. 5-day per-phase escalation guard.

## Phase-expansion policy

If diagnosis of a phase reveals close-sibling bugs in the same MP method or close protocol, fix them in the same phase. **Do NOT** expand to unrelated areas. If the iteration's scope balloons past 5 days or the bug count rises past 3 within one iteration, escalate per rule 3.

In particular for Phase 2 (inspect break): if the root cause turns out to be a deep stdlib refactor or multiple compounding bugs, escalate. Inspect.py is a key dependency; rather than letting Phase 2 sprawl, prefer a targeted workaround in inspect.py itself (one-line conditional) and file the deeper bug as a B-DD entry.

## Tracking table (kept current through execution)

| # | Phase | Status | Closed by commit | Notes |
|---|---|---|---|---|
| C1 | `in` MP through `__contains__` | closed | ba1acb46 | reproducer 10/10; py_mappingproxy_contains tightened to hasOwnAttribute; inspect cascade is the known C2 follow-up |
| C2 | `import inspect` works after C1 | open | — | |
| C3 | 6 MP methods own-only | open | — | |
| C4 | SP-B/B3 verified closed; audit re-run delta documented | open | — | |

Per-phase implementer updates this table on each commit.

## Deliverables

| Artifact | Persists? |
|---|---|
| `tests/synthetic/sp_c_phase{1,2,3,4}_repro.py` (4 reproducers) | Yes — regression tests |
| Fixes in `src/library/ExecutionEngine.cpp` (Phase 1), `src/library/PythonEnvironment.cpp` (Phase 3) | Yes |
| Possible fix in `lib/python3.14/inspect.py` (Phase 2 only if root cause lives there) | Yes |
| ≥4 commits (one per phase, more if sub-bugs surface) | Yes |
| SP-B tracking-table update — B3 row marked `closed (by SP-C, commit <SHA>)` | Yes |
| Audit document (`2026-04-30-protopy-ground-truth-audit.md`) — new "SP-C re-run" section | Yes |
| New `CPYTHON_CONFORMANCE.md` entry V155.x reflecting MappingProxy semantics + B3 closure | Yes |
| Re-run audit shows cluster-2 B3 cleared | Yes (rerun on demand) |

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Phase 2 (inspect break) reveals cascading stdlib dependencies | 5-day guard; targeted workaround in inspect.py acceptable rather than fixing every downstream caller |
| One of the 6 MP methods has external clients legitimately wanting inherited attrs | Per-method test in sp_c_phase3_repro.py; full regression check (custom Necessary + synthetic suites) before each commit |
| Phase 1's compareOp fix interacts with OP_CONTAINS_OP (CPython 3.11+ separate opcode) | Diagnosis step — grep for OP_CONTAINS_OP in ExecutionEngine.cpp; if separate handler exists, apply fix to both |
| The B-DD1 (LOAD_NAME closure-chain PROTO_NONE) latent landmine triggers during diagnosis | Already deferred per SP-B; if it surfaces in SP-C, document and proceed |
| Some legitimately uses `cls.__dict__['__init__']` to access the class-bound init slot (vs inherited) | Per CPython that raises KeyError for inherited; if any test relies on the protopy bug, rewrite the test |

## Bridge back to SP-B

When SP-C closes:

- Edit `2026-04-30-sp-b-attribute-resolution-bugs-design.md` tracking table:
  - B3 row: status `closed (by SP-C, commit <final-SHA>)`.
- Edit `2026-04-30-protopy-ground-truth-audit.md`: add an "SP-C re-run" section comparing the cluster-2 status before vs after.
- Edit `CPYTHON_CONFORMANCE.md`: add V155.x entry summarizing MappingProxy semantics fix, B3 closure, and any test status improvements (likely test_dataclasses moves from CRASH to FAIL_UNITTEST or further).

SP-B remains PAUSED with B4, B5-reraise, B-DD1, B-DD2 still deferred. A future SP-B-resume sub-project (or SP-D) can pick those up.

## Next step

User reviews this spec. After approval, the brainstorming flow invokes `superpowers:writing-plans` to produce the executable implementation plan.
