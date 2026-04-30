# SP-B — Attribute-Resolution Bugs (Cluster 2) Design

**Status:** Draft, pending user review
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
| B1 | `ABCMeta.gen` missing | open | — | |
| B2 | `ArgumentParser.conflict_handler` missing | open | — | |
| B3 | `Point.x` missing (dataclass) | open | — | |
| B4 | `socket` descriptor formatting | open | — | |
| B5 | `typing.py:20 NoneType` + `reraise outside except` | open | — | |

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
