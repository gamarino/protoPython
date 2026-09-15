# SP1 — Test Infrastructure Audit & Repair (Design)

**Status:** PAUSED on 2026-04-29. SP1 was started but Phase 1 (audit probes) revealed six base-interpreter bugs that block the audit infrastructure itself: `json.dumps(int)` returns garbage, `sys.path.insert` does not affect import resolution, local-module imports leak `DEBUG:` to stdout, `import traceback` silently aborts the script, `except Exception as e` may not catch what `BaseException` catches, and attribute access on a caught exception (`type(e).__name__`) crashes silently. The audit-driven approach assumed the interpreter base was stable; that premise is wrong. SP1 is paused pending **SP0 — interpreter base stabilization**, which will fix the prerequisites. After SP0 completes, SP1 resumes from the v2 plan.

**Original status:** Draft, pending user review
**Author:** brainstorming session, 2026-04-29
**Project:** protoPython
**Sub-project:** SP1 of the multi-spec push to "All Green" Essential CPython conformance

## Goal

Identify and repair every protoPython gap that prevents the CPython test-infrastructure modules — `test.support`, `doctest`, `inspect`, `annotationlib`, `unittest` — from working correctly enough to run the Essential category of `Lib/test/`.

The infrastructure modules are already physically present in `lib/python3.14/`. SP1 is **directed surgery on the protoPython gaps that those modules hit at runtime**, not construction from scratch.

## Non-goals

- Replacing or rewriting any of the five infrastructure modules. We use CPython's own source as-is and fix the protoPython gaps the modules expose.
- Making every gap green. The Z filter (see Triage) drops gaps that no Essential test exercises.
- Implementing other Essential tests' feature gaps (PEP 649, metaclass protocol, async generator internals). Those are SP2-SP6.

## Architecture — Three Phases

### Phase 1: Audit

Write five self-probes in `tests/audits/probe_<module>.py`. Each probe exercises the public, documented API surface of one module. Probes have uniform structure so the triage step can process them mechanically.

Probe shape:

```python
# tests/audits/probe_<module>.py
import json, traceback

gaps = []

def expect(name, fn, expected=None):
    """Run fn(); record success/failure with full context."""
    try:
        result = fn()
        gaps.append({"id": name, "status": "PASS",
                     "result": repr(result)[:200]})
    except Exception as e:
        gaps.append({
            "id": name, "status": "FAIL",
            "exc_type": type(e).__name__,
            "exc_msg": str(e),
            "traceback": traceback.format_exc(),
        })

# 30-50 expect() calls covering the module's public API ...

print(json.dumps(gaps, indent=2))
```

Coverage per probe (minimum API surface):

| Probe | API surface |
|---|---|
| `probe_test_support.py` | `captured_stdout`, `EnvironmentVarGuard`, `check_syntax_error`, `run_unittest`, `requires_*`, `import_helper.*`, `os_helper.*`, `threading_helper.*`, `refleak_helper.*`, `warnings_helper.*` |
| `probe_doctest.py` | `testmod`, `testfile`, `run_docstring_examples`, `DocTestSuite`, `DocTestParser.parse / get_examples`, `OutputChecker.check_output`, options (`ELLIPSIS`, `NORMALIZE_WHITESPACE`, `IGNORE_EXCEPTION_DETAIL`, `SKIP`, `DONT_ACCEPT_TRUE_FOR_1`), `DebugRunner` |
| `probe_inspect.py` | `signature`, `Parameter`, `BoundArguments`, `getmembers`, `getfullargspec`, `getsource`, `getsourcelines`, `getmodule`, `currentframe`, `stack`, `getframeinfo`, `isfunction / ismethod / isclass / isgenerator / iscoroutine`, `classify_class_attrs`, `get_annotations` |
| `probe_annotationlib.py` | `get_annotations`, `ForwardRef`, `Format` (VALUE / FORWARDREF / STRING / SOURCE), `call_evaluate_function`, `call_annotate_function` |
| `probe_unittest.py` | `TestCase`, `assertEqual / Raises / Almost / In / Not*`, `subTest`, `skip / skipIf / skipUnless`, `expectedFailure`, `setUp / tearDown / setUpClass`, `TestSuite`, `TestLoader.loadTestsFromModule`, `TextTestRunner`. **Includes `mock.Mock / patch`**; the Z filter drops them if no Essential exercises them. |

Each probe is run via `PROTOJS_USE_PROTO_EVAL=1 ./build/protopy tests/audits/probe_<module>.py > audits/<module>.json`. The five JSON outputs are the input to Phase 2.

### Phase 2: Triage (Z filter)

For each `FAIL` entry across the five probe outputs, classify and decide:

**Root-cause classification:**

| Category | Meaning | Where the fix lives |
|---|---|---|
| `INTERP` | Compiler / interpreter bug (opcode, descriptor protocol, MRO, frame semantics) | `protoPython/src/` C++ |
| `NATIVE` | Missing or incomplete native stub (e.g., `_thread.X`, `_io.Y`) | `protoPython/src/library/` C++ |
| `LIB` | Bug in the Python-level stdlib code (e.g., `enum.py`, `argparse.py`) | `protoPython/lib/python3.14/*.py` |
| `TEST` | Probe bug or wrong expectation | The probe itself |

**Z-filter algorithm** (applied to every FAIL):

1. Identify which Essential test(s) — `test_grammar.py`, `test_types.py`, `test_descr.py`, `test_generators.py`, `test_asyncgen.py`, `test_base64.py`, `test_json.py` — depend on the broken API. Search via `grep` and reading the test source.
2. If at least one Essential test depends on it → **ENTERS the backlog** at priority P0 (≥3 tests) / P1 (1-2 tests).
3. If no Essential test depends but the API is **load-bearing** (criterion: used by ≥3 helpers downstream, or documented as a prominent public API) → **ENTERS the backlog** at P2 with written justification.
4. Otherwise → **ARCHIVED** in `docs/superpowers/specs/2026-04-29-sp1-archived.md`.

**Backlog entry format:**

```markdown
## Gap SP1-G023: doctest.OutputChecker ELLIPSIS does not match

- **Probe**: `probe_doctest.py::ellipsis_basic`
- **Category**: LIB (suspected) — verify with quick INTERP check first
- **Repro**: `doctest.OutputChecker().check_output("...", "abc", optionflags=doctest.ELLIPSIS)` returns `False`, expected `True`
- **Essential impact**: `test_generators.py` uses doctest with implicit ELLIPSIS; failure cascades.
- **Effort estimate**: 0.5 - 1 day
- **Priority**: P1
- **Status**: backlog | in_progress | done (commit SHA) | blocked
```

Backlog is written to `docs/superpowers/specs/2026-04-29-sp1-audit-backlog.md`.

**Volume expectation:** five probes likely surface 50-80 FAILs. The Z filter should leave 15-25 in the backlog. If filtered count exceeds 30, the controller pauses and asks the user whether to recap.

### Phase 3: Implementation (TDD per gap)

For each backlog entry in priority order:

1. **Write minimal reproducer** — usually a 5-20 line script that triggers the FAIL.
2. **Verify it fails** — running the reproducer confirms the symptom.
3. **Identify the root cause** — read the relevant protoPython source. Confirm classification (INTERP/NATIVE/LIB).
4. **Fix it** — surgical change in the right layer. Follow existing patterns (perpetual symbols via `createSymbol`, async pin via `ProtoRootSet`, etc.).
5. **Verify reproducer passes.**
6. **Verify ≥1 Essential test moved fail→pass** — re-run the implicated Essential test. If the gap was correctly identified the test should advance. If not, document why (most common: secondary blocker — the gap was real but a deeper one is now exposed; that deeper one is a new backlog entry).
7. **Verify no regression** — synthetic suite (37/0/0) and custom suites (`test_decorator`, `test_abc`, `test_contextlib`, `test_dataclasses`) must remain green.
8. **Commit** with message `"<module>: fix <gap-id> — <one-line description>"` and reference to the backlog entry. Update backlog status to `done (commit SHA)`.

**Process invariant:** every commit closes exactly one backlog entry. Multi-gap commits are forbidden — they make rollback expensive.

## Stop Condition

Phase 3 ends when **either**:

- All filtered gaps are `done`, OR
- The cap is reached: **4 weeks elapsed since Phase 3 started** OR **20 commits in Phase 3**, whichever comes first.

If the cap is reached with backlog remaining, the unresolved entries become input to a follow-up sub-project (SP1.5 or merged into SP2-6 if the gaps cluster naturally with those subsystems).

## Deliverables

| File / artifact | Purpose | Persists? |
|---|---|---|
| `tests/audits/probe_*.py` (5 files) | Self-probes | Yes — kept as regression tests |
| `tests/audits/<module>.json` (5 files, per run) | Phase 1 output | No — regenerated per audit |
| `docs/superpowers/specs/2026-04-29-sp1-audit-backlog.md` | Phase 2 output, updated through Phase 3 | Yes |
| `docs/superpowers/specs/2026-04-29-sp1-archived.md` | Filtered-out FAILs (with rationale) | Yes |
| `docs/CPYTHON_CONFORMANCE.md` (Essential table) | Updated row for each Essential test that moved | Yes |
| Git commits in `master` | One per gap | Yes |

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Probes themselves have bugs (false FAILs) | TEST category in triage; probes are reviewed before Phase 2 |
| A single gap turns out to be very large (>1 week) | Escalate to user; consider promoting it to its own SP and skipping in SP1 |
| Z filter is too aggressive, drops something important | Priority P2 (load-bearing) catches non-test items; user reviews the archived list once |
| Essential test is blocked by gap not surfaced by any probe | Re-run the Essential test after each fix; if a new gap appears, add it to the backlog manually |
| GC bridging gets violated by a fix | All async-lambda captures audited per `protoPython/CLAUDE.md` rules before commit |

## What success looks like

At the end of SP1, the Essential row of `CPYTHON_CONFORMANCE.md` should look measurably better — the exact number depends on what the audit finds. Concrete expectations:

- `test_generators.py`: 0 → 1 (doctest fix likely sufficient)
- `test_grammar.py`: 54 → 60+ (test.support helpers + inspect introspection unblock 5-10 of the 21 failures)
- `test_base64.py`: PARTIAL → meaningfully better (unittest.subTest will likely surface)
- `test_types.py` / `test_descr.py`: gentler improvement; main blockers are SP4-SP5 territory
- `test_asyncgen.py`: probably no change — main blocker is SP6 territory

A secondary success: the 5 probes remain as **permanent regression tests** that the CI can run after any future change to either the interpreter or the affected stdlib modules.

## Next step

User reviews this spec. After approval, the brainstorming flow invokes `superpowers:writing-plans` to produce the executable implementation plan (per-task TDD steps with file paths and commands).
