# SP0 — Interpreter Base Stabilization (Design)

**Status:** Draft, pending user review
**Author:** brainstorming session, 2026-04-30
**Project:** protoPython
**Sub-project:** SP0 — prerequisite for SP1 (test-infrastructure audit & repair)

## Goal

Stabilize protoPython's interpreter base by fixing the six bugs that the SP1 audit infrastructure surfaced — bugs that prevent even the audit machinery from running. Deliver a permanent regression smoke suite so the same kind of base-level breakage cannot regress unnoticed.

## Background — how SP0 came to exist

SP1 was originally framed as "directed surgery on the protoPython gaps that the existing test-infrastructure modules hit at runtime", assuming the base interpreter was stable. Phase 1 of SP1 (writing five JSON-emitting probes) revealed six bugs in the base interpreter that block the audit infrastructure itself:

| Bug | Symptom |
|---|---|
| **A** | `json.dumps(int)` returns `<NoneType object at (nil)>` instead of the integer; `json.dumps(str)` works |
| **B** | `sys.path.insert(0, p)` updates `sys.path` but does not affect `import` resolution |
| **C** | Importing a local module emits ~240 lines of `DEBUG:` to **stdout** (not stderr) |
| **D** | `import traceback` raises an exception that silently aborts the script |
| **E** | `except Exception as e:` may not catch what `except BaseException` catches |
| **F** | Attribute access on a caught exception (e.g. `type(e).__name__`) crashes silently |

D, E, and F are likely related (exception machinery). A, B, and C are independent.

After workaround attempts on Task 1.0 of SP1 surfaced more bugs faster than they could be hidden, SP1 was paused. SP0 fixes the prerequisites; SP1 resumes after SP0.

## Non-goals

- Comprehensive interpreter audit. SP0 fixes the six bugs (and their close siblings if discovered while diagnosing) — not every protopy correctness gap.
- Reproducing the SP1 audit-driven approach inside SP0. SP0 uses direct TDD reproducers, no JSON probe infrastructure (which is broken anyway).
- Performance work. SP0 is correctness-only.

## Architecture — Five Phases (linear, quick-wins-first)

The phases are ordered to build momentum: small / well-localized fixes first, the deepest one (exceptions) last when the diagnostic machinery built by earlier phases (clean stdout, working json, sys.path-aware imports) makes diagnosis easier.

### Phase 1 — DEBUG printf cleanup (bug C)

**Goal:** importing a local module no longer emits `DEBUG:`-prefixed lines on stdout.

**Likely files:** `src/runtime/{ImpModule.cpp, Compiler.cpp, ExecutionEngine.cpp, PythonEnvironment.cpp}` and `src/library/*.cpp`. Diagnose by `grep -nE 'fprintf\\(stdout|std::cout|printf\\("DEBUG' src/`.

**Done when:** `protopy script_that_imports_a_local_module.py 2>/dev/null | grep -c '^DEBUG:'` returns `0`. Stderr DEBUG output (gated by `PROTO_ENV_DIAG`) is acceptable; stdout DEBUG is not.

**Estimate:** ~1 day.

### Phase 2 — JSON encoder int unboxing (bug A)

**Goal:** `json.dumps(5)` returns `"5"`, `json.dumps({"k": 1})` returns `'{"k": 1}'`, nested ints round-trip correctly.

**Likely files:** `lib/python3.14/json/encoder.py` (where the int path lives) plus the C++ helper that unboxes `SmallInteger` for str/repr conversion. May overlap with `int.__repr__` or the formatter that the encoder calls.

**Diagnose by:** read `JSONEncoder.iterencode` and follow the int branch; identify why a tagged-pointer `SmallInteger` becomes `<NoneType object at (nil)>` somewhere along the call chain. Compare with how `repr(5)` works (which already produces `"5"` correctly).

**Done when:** `json.dumps({"int": 1, "list": [2, 3], "nested": {"x": 4}})` returns the canonical form, character-for-character.

**Estimate:** ~1 day. If the bug expands to "json encoder is broken for several types, not just int", we fix all encoder-type gaps in this phase.

### Phase 3 — sys.path runtime mutation (bug B)

**Goal:** `sys.path.insert(0, "/tmp/x")` after which `import foo` resolves `/tmp/x/foo.py`.

**Likely files:** `src/runtime/ImpModule.cpp` `resolveModule()` (or equivalent). Probable cause: the resolver reads a snapshot of `sys.path` taken at startup, not the live `sys.path` list. Fix is to consult `sys.modules['sys'].path` on every resolve.

**Done when:** the Phase 3 reproducer passes (a temp file written under `tempfile.mkdtemp()`, `sys.path.insert`, `import _sp0_temp` succeeds).

**Estimate:** ~2-3 days (medium — interpreter import path is non-trivial).

### Phase 4 — Exception machinery (bugs D, E, F)

**Goal:**

- `import traceback` succeeds and exposes `format_exc()`, `print_exc()`, `format_exception()`.
- `try: raise ValueError("x"); except Exception as e:` binds `e` correctly so `e.args == ("x",)`.
- `try: 1/0; except BaseException as e:` followed by `type(e).__name__` returns `"ZeroDivisionError"` without aborting.
- `traceback.format_exc()` returns a non-empty string after a caught exception.

**Likely files:** `src/runtime/ExecutionEngine.cpp` (try/except dispatch, exception unwinding), `src/runtime/Compiler.cpp` (compileTry, except-as binding bytecode emission), `src/runtime/PythonEnvironment.cpp` (exception class hierarchy and `BaseException`/`Exception` setup), and possibly `lib/python3.14/traceback.py` (if the import failure is rooted in a stdlib feature gap).

**Diagnose by:** start with `import traceback`. Run under `PROTO_ENV_DIAG=2` to capture the bytecode trace. Identify where the silent abort happens (likely in an opcode that returns nullptr without setting an exception). Use `gdb --args build/src/runtime/protopy /tmp/import_traceback.py` if needed; protopy is built with debug symbols in `build-debug/`.

**Done when:** the Phase 4 reproducer (4a, 4b, 4c, 4d) passes.

**Estimate:** ~5 days (the deepest bug; if it expands beyond 1 week, escalate).

### Phase 5 — Smoke suite consolidation

**Goal:** `tests/synthetic/sp0_smoke.py` runs all 4 phase reproducers and reports green.

**Done when:** the smoke suite exits 0 and prints "ALL PHASES PASS".

**Estimate:** ~0.5 day.

## Per-Phase TDD Cycle

Each phase follows the same eight-step cycle:

1. **Write minimal reproducer** in `tests/synthetic/sp0_phase{N}_repro.py` (5-15 lines).
2. **Verify it fails** on current protopy: `./build/src/runtime/protopy tests/synthetic/sp0_phase{N}_repro.py`.
3. **Diagnose** by reading the relevant source. Use `gdb` if the failure mode is a silent abort.
4. **Fix** in C++ (`src/runtime/`, `src/library/`) or Python (`lib/python3.14/`) per root cause.
5. **Build:** `cmake --build build 2>&1 | tail -5`. Must compile clean.
6. **Verify reproducer passes.**
7. **Regression check** (must all remain green):
   - protoCore tests: 136/136
   - Synthetic generators+async suite: 37/0/0
   - Custom suites: `test_decorator`, `test_abc`, `test_contextlib`, `test_dataclasses` all PASS
8. **Commit** with message format `<area>: fix SP0-PN — <one-line>`. One commit per bug; if a phase has N sub-bugs (e.g., 4a/4b/4c counted separately), N commits.

## Reproducers (canonical form)

```python
# tests/synthetic/sp0_phase1_repro.py
# DEBUG stdout cleanliness — fail if local-module import emits "DEBUG:" on stdout.
import os
print("MARKER_PHASE1", flush=True)
# Driver checks: stdout has no "DEBUG:" lines AND has "MARKER_PHASE1".
```

```python
# tests/synthetic/sp0_phase2_repro.py
# JSON encoder int unboxing.
import json

assert json.dumps(5) == "5", f"int dumps: {json.dumps(5)!r}"
assert json.dumps({"k": 1}) == '{"k": 1}', \
    f"dict dumps: {json.dumps({'k': 1})!r}"
assert json.dumps([2, 3, 4]) == "[2, 3, 4]", \
    f"list dumps: {json.dumps([2, 3, 4])!r}"
assert json.dumps({"int": 1, "list": [2, 3], "nested": {"x": 4}}) \
    == '{"int": 1, "list": [2, 3], "nested": {"x": 4}}'
print("PHASE2_OK")
```

```python
# tests/synthetic/sp0_phase3_repro.py
# sys.path runtime mutation.
import sys, os, tempfile

d = tempfile.mkdtemp()
with open(os.path.join(d, "_sp0_temp.py"), "w") as f:
    f.write('def hello(): return "ok"\n')
sys.path.insert(0, d)
import _sp0_temp
assert _sp0_temp.hello() == "ok"
print("PHASE3_OK")
```

```python
# tests/synthetic/sp0_phase4_repro.py
# Exception machinery.
import traceback
assert hasattr(traceback, "format_exc")

# 4b: except-as binding preserves args
try:
    raise ValueError("foo")
except Exception as e:
    assert e.args == ("foo",), f"e.args={e.args!r}"

# 4c: type(e).__name__ on caught exception
try:
    1 / 0
except BaseException as e:
    assert type(e).__name__ == "ZeroDivisionError"

# 4d: traceback.format_exc returns a string with the exception type and message
try:
    raise RuntimeError("trace test")
except Exception:
    s = traceback.format_exc()
    assert "RuntimeError" in s and "trace test" in s

print("PHASE4_OK")
```

```python
# tests/synthetic/sp0_smoke.py
# SP0 regression smoke — runs all 4 phase reproducers.
import subprocess, sys, os

PHASES = [
    ("tests/synthetic/sp0_phase1_repro.py", "MARKER_PHASE1"),
    ("tests/synthetic/sp0_phase2_repro.py", "PHASE2_OK"),
    ("tests/synthetic/sp0_phase3_repro.py", "PHASE3_OK"),
    ("tests/synthetic/sp0_phase4_repro.py", "PHASE4_OK"),
]
PROTOPY = os.environ.get("PROTOPY", "./build/src/runtime/protopy")

failed = []
for phase, marker in PHASES:
    print(f"=== {phase} ===")
    env = {**os.environ, "PROTO_ENV_DIAG": "0"}
    p = subprocess.run([PROTOPY, phase], capture_output=True, text=True, env=env)
    has_debug_stdout = any(l.startswith("DEBUG:") for l in p.stdout.splitlines())
    has_marker = marker in p.stdout
    ok = (p.returncode == 0 and has_marker and not has_debug_stdout)
    print(p.stdout[-500:])
    if not ok:
        failed.append(phase)

if failed:
    print(f"\nFAILED: {failed}")
    sys.exit(1)
print("\nALL PHASES PASS — SP0 stabilization regression smoke green")
```

## Stop Condition

SP0 ends when **all** are true:

- Every reproducer (`sp0_phase{1,2,3,4}_repro.py`) exits 0 with the expected MARKER on stdout.
- `sp0_smoke.py` prints "ALL PHASES PASS".
- Synthetic generators+async suite: 37/0/0 (no regression).
- Custom Necessary suites: 4/4 PASS.
- protoCore C++ tests: 136/136.

No time-cap. Foundational work, not ROI-bounded. If Phase 4 expands beyond one week wall-clock, escalate to user before continuing.

## Phase-Expansion Policy

If diagnosing a phase reveals additional close-sibling bugs (same file, same function, same protocol), fix them in the same phase. The user explicitly chose "broad sweep" rather than minimal patching; sibling bugs caught while we are already in the source area cost little extra and prevent the next iteration from rediscovering them.

If sibling bugs balloon the phase past 1 week, escalate. The escalation triggers a sub-decision: (a) split the expanded scope into a new phase or sub-project, (b) accept the wall-clock and continue, or (c) revert to minimal scope and defer the siblings.

## Bridge Back to SP1

When SP0 closes, the SP1 v2 plan's workarounds become unnecessary. Specifically:

- `_probe_lib.py` (currently committed at `cff36064` in v2 form) can revert to the v1 idiomatic shape (`json.dumps(big_dict)`, `import traceback`, `traceback.format_exc()`, `except Exception as e:`).
- All probes can drop the cwd-must-be-`tests/audits/` constraint (sys.path.insert works).
- `run_all.sh` can drop the `grep -v "^DEBUG:"` post-filter.

A small "SP1 resume" commit reverts the probe library to v1, updates the SP1 spec from "PAUSED" to "Active", and SP1 Phase 1 resumes from Task 1.1 (the test.support probe).

## Deliverables

| Artifact | Persists? |
|---|---|
| `tests/synthetic/sp0_phase{1,2,3,4}_repro.py` (4 reproducers) | Yes — regression tests |
| `tests/synthetic/sp0_smoke.py` (consolidated smoke) | Yes — CI-friendly regression suite |
| Fixes in `src/runtime/*.cpp`, `src/library/*.cpp`, `lib/python3.14/*.py` | Yes |
| ≥4 commits (one per fixed bug; more if sub-bugs surface) | Yes |
| New entry in `docs/CPYTHON_CONFORMANCE.md` (V155.0 — SP0 closure) | Yes |
| `docs/superpowers/specs/2026-04-29-sp1-test-infra-audit-design.md` PAUSED → Active | Yes |
| Revert of `_probe_lib.py` from v2 (workaround) to v1 (idiomatic) | Yes |

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Phase 4 (exceptions) becomes a multi-week project | Time-box to 5 days; escalate before continuing |
| A fix introduces regressions in the synthetic / custom suites | Step 7 of the cycle is mandatory; never commit broken |
| A 7th bug surfaces during diagnosis | If it blocks SP1 directly, add a Phase to SP0; otherwise archive in a follow-up doc |
| The exception class hierarchy is more broken than expected (E may unblock Z others) | Phase 4 explicitly allows expansion; if E is one of N hierarchy bugs we fix all in Phase 4 |
| The DEBUG printf is in a hot path and removing it changes output ordering of legitimate prints | Verify by running existing custom suites after Phase 1; their stdout transcripts are stable |

## Next step

User reviews this spec. After approval, the brainstorming flow invokes `superpowers:writing-plans` to produce the executable implementation plan.
