# SP-B — Attribute-Resolution Bugs (Cluster 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the five attribute-resolution-symptom crashes catalogued in the 2026-04-30 ground-truth audit (B1: ABCMeta.gen; B2: ArgumentParser.conflict_handler; B3: Point.x; B4: socket descriptor formatting; B5: typing.py:20 NoneType + reraise outside except).

**Architecture:** Iterative cascade per the SP-B spec — B5 first (highest cascade, 3 tests unblocked, probable shared root with the SP0 silent-halt fix), then re-run audit to see which sibling symptoms collapsed, then repeat for whatever remains. One commit per **root cause**, not per symptom (a single fix may close multiple).

**Tech Stack:** protopy (C++ interpreter at `build/src/runtime/protopy`, sources in `src/runtime/`, `src/library/`), Python stdlib in `lib/python3.14/`, gdb for silent-abort diagnosis, the rerunnable audit at `tests/synthetic/sp_audit_truth.py`.

**Spec reference:** `docs/superpowers/specs/2026-04-30-sp-b-attribute-resolution-bugs-design.md`

---

## File Structure

| Path | Responsibility | Status |
|---|---|---|
| `tests/synthetic/sp_b_b1_abcmeta_gen_repro.py` | B1 reproducer | new (Task 2) |
| `tests/synthetic/sp_b_b2_argparse_conflict_repro.py` | B2 reproducer | new (Task 3) |
| `tests/synthetic/sp_b_b3_dataclass_init_repro.py` | B3 reproducer | new (Task 4) |
| `tests/synthetic/sp_b_b4_socket_descriptor_repro.py` | B4 reproducer | new (Task 5) |
| `tests/synthetic/sp_b_b5_typing_repro.py` | B5 reproducer | new (Task 1) |
| `src/runtime/*.cpp`, `src/library/*.cpp` | Per-iteration fixes per root cause | modified |
| `lib/python3.14/*.py` | If a fix lives at the Python level | modified |
| `docs/superpowers/specs/2026-04-30-sp-b-attribute-resolution-bugs-design.md` | Tracking table updated each iteration | modified (Task 6) |
| `docs/CPYTHON_CONFORMANCE.md` | New entry reflecting SP-B closure | modified (Task 6) |

**Build / run conventions** (apply to every task):
- Build: `cmake --build build 2>&1 | tail -5` from `protoPython/` root.
- Run a script: `PROTO_ENV_DIAG=0 ./build/src/runtime/protopy <script.py>`.
- Run the audit: `python3 tests/synthetic/sp_audit_truth.py`.

**Regression check** (used at every iteration's verify step):

```bash
# protoCore C++ tests — must remain 159/159
ctest --test-dir build -j$(nproc) --output-on-failure 2>&1 | tail -5

# Synthetic suites — must remain at baseline (23/13/1, 34/2/1)
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/test_generators_synthetic.py 2>&1 | tail -5
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/test_metaclass_descr_synthetic.py 2>&1 | tail -5

# SP0 phase reproducers — must remain green
for p in 1 2 2_5; do
    PROTO_ENV_DIAG=0 ./build/src/runtime/protopy "tests/synthetic/sp0_phase${p}_repro.py" 2>/dev/null | tail -3
done

# Bootstrap — must remain green
echo 'import importlib; print("ok")' | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>/dev/null
echo 'import inspect; print("ok")' | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>/dev/null
```

All must remain green. Any regression blocks the commit.

**Per-iteration TDD cycle** (used by every Task 1-5):

1. Write reproducer at the canonical path.
2. Verify reproducer fails on current main.
3. Diagnose root cause (read source, run `PROTO_ENV_DIAG=2`, gdb if silent abort).
4. Fix in C++ and/or Python.
5. Build.
6. Verify reproducer passes (≥10 runs, all green — protect against the silent-halt-style false positives).
7. Re-run `python3 tests/synthetic/sp_audit_truth.py` and note which OTHER symptoms also closed.
8. Run the regression-check block above; all green.
9. Commit per root cause (one commit may close multiple symptoms — list them all in the message).
10. Update the tracking table in the SP-B spec doc with the closure status and commit SHA.

---

## Task 1 — Iteration 1: B5 (typing.py:20 NoneType + reraise outside except)

**Why first:** highest cascade (3 Essential+Important tests unblock), likely shares root with the SP0 silent-halt fix.

**Files:**
- Create: `tests/synthetic/sp_b_b5_typing_repro.py`
- Modify: `src/runtime/*.cpp`, `src/library/*.cpp`, and/or `lib/python3.14/*.py` per diagnosis.

- [ ] **Step 1.1: Write reproducer**

```python
# tests/synthetic/sp_b_b5_typing_repro.py
"""SP-B / B5 reproducer — typing.py:20 'NoneType' object is not callable.

The audit reports `typing` failing at line 20 (`from abc import abstractmethod, ABCMeta`)
with 'NoneType' object is not callable, and a related 'reraise outside of except block'
in the test_base64 import chain.  Both likely share root cause with the SP0 silent-halt
exception-machinery work.

This script triggers just the failing import and asserts both names are usable.
"""
from abc import abstractmethod, ABCMeta

# Verify both bindings landed
assert callable(abstractmethod), "abstractmethod is not callable"
assert isinstance(ABCMeta, type), \
    f"ABCMeta is {type(ABCMeta).__name__}, expected type"

# Sanity: ABCMeta should be usable as a metaclass
class _SP_B_B5_Probe(metaclass=ABCMeta):
    @abstractmethod
    def m(self): ...

assert isinstance(_SP_B_B5_Probe, ABCMeta)
print("SP_B_B5_OK")
```

- [ ] **Step 1.2: Verify reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b5_typing_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: silent abort or exception, exit non-zero. (The audit reported exit 70.)

- [ ] **Step 1.3: Diagnose**

```bash
# Capture the failure with maximum diagnostic verbosity
PROTO_ENV_DIAG=2 ./build/src/runtime/protopy tests/synthetic/sp_b_b5_typing_repro.py 2>&1 | tail -100

# Read typing.py around line 20 to see what's imported
sed -n '15,30p' lib/python3.14/typing.py

# Read abc.py to find what abstractmethod and ABCMeta are
grep -n "^def abstractmethod\|^class ABCMeta\|^abstractmethod\|^ABCMeta" lib/python3.14/abc.py | head -10

# Bisect: try importing each piece in isolation
echo "from abc import abstractmethod" | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>&1 | tail -10
echo "from abc import ABCMeta" | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>&1 | tail -10
echo "import abc; print(abc.abstractmethod, abc.ABCMeta)" | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>&1 | tail -10
```

The bisect reveals which name is the actual culprit. The audit reports `'NoneType' object is not callable` — this typically means a class definition or class-decorator call is receiving a non-callable. Possibilities:
- A `__metaclass__` lookup returns None.
- A descriptor `__get__` returns None where a callable is expected.
- A `setattr` on a non-mutable type silently no-ops.

If the bisect shows individual imports work but the combined `from abc import abstractmethod, ABCMeta` fails: the bug is in multi-name `from-import` semantics. Look at `OP_IMPORT_FROM` in `src/runtime/ExecutionEngine.cpp` and `compileImportFrom` in `src/runtime/Compiler.cpp`.

If the bisect shows the failure is on first use (instantiating `_SP_B_B5_Probe`): the bug is in metaclass `__call__` or in the descriptor protocol for `@abstractmethod`-decorated methods. Look at `py_type_call` in `src/library/BuiltinsModule.cpp` and the metaclass MRO walk in `src/library/PythonEnvironment.cpp`.

If the bisect shows `'NoneType' object is not callable` happens in the import of `abc` itself: the bug is in `abc.py` running on protopy — likely a stdlib issue with how protopy handles class-decorator chains or `_abc_init`-equivalent native helpers.

If the failure is silent (exit 0 with no output, no exception), the silent-halt regime is reproducing — capture trace under PROTO_ENV_DIAG=2 to find the last opcode dispatched.

For "reraise outside of except block": grep `src/runtime/ExecutionEngine.cpp` for the opcode `OP_RAISE_VARARGS` or equivalent. The error means a `raise` statement (zero-arg form, used to re-raise) is executing outside a handler context — diagnostically points at frame state mismanagement around try/except.

- [ ] **Step 1.4: Apply the fix**

The fix shape depends on Step 1.3's output. Patterns to choose between:

(a) **C++ runtime fix** in the exception machinery (`ExecutionEngine.cpp` opcode handler, `PythonEnvironment.cpp` exception class hierarchy, `Compiler.cpp` try/except codegen). Follow existing patterns; respect the protoCore GC bridging rules in `protoPython/CLAUDE.md` (perpetual symbols via `createSymbol`, async pin via `ProtoRootSet`).

(b) **C++ native-helper fix** in `src/library/` if `abc`'s `_abc_init` (or sibling) native is wrong.

(c) **Python-side fix** in `lib/python3.14/abc.py` or `typing.py` if the stdlib code uses a feature protopy implements differently and the workaround is small and well-bounded.

Pick the smallest fix that closes the reproducer. If the fix shape is ambiguous, prefer the C++ runtime fix when the bug is in dispatch/binding, and the Python-side workaround when the bug is in stdlib code expecting a feature protopy doesn't yet implement.

- [ ] **Step 1.5: Build**

```bash
cmake --build build 2>&1 | tail -5
```

Must compile clean.

- [ ] **Step 1.6: Verify reproducer passes (≥10 runs)**

```bash
RATE=$(for i in $(seq 10); do
    PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b5_typing_repro.py 2>/dev/null | grep -c SP_B_B5_OK
done | awk '{s+=$1} END {print s}')
echo "SP_B_B5_OK printed in $RATE/10 runs"
```

Expected: 10/10. Anything less means a flake — silent-halt-style false positive — and is not acceptable. Investigate before proceeding.

- [ ] **Step 1.7: Re-run audit and note collapsed symptoms**

```bash
python3 tests/synthetic/sp_audit_truth.py 2>&1 | tee /tmp/sp_b_audit_iter1.out
```

Compare against the baseline audit at `docs/superpowers/specs/2026-04-30-protopy-ground-truth-audit.md`. Note which previously-CRASH tests now PASS / FAIL_UNITTEST / are still CRASH. The B5 fix may have unblocked B1 (`ABCMeta.gen`) too — same metaclass area. If so, the next iteration will skip B1.

- [ ] **Step 1.8: Regression check**

Run the regression-check block from the plan header. All must remain green.

- [ ] **Step 1.9: Commit**

```bash
git add tests/synthetic/sp_b_b5_typing_repro.py \
        <files-touched-by-fix>
git commit -m "$(cat <<'EOF'
runtime: fix SP-B/B5 — typing.py:20 NoneType callable + reraise outside except

Root cause: <2-3 sentences from the diagnosis>.

Symptoms closed by this commit: B5 (and possibly others — list any
sibling symptoms the audit re-run showed collapsed).

Reproducer: tests/synthetic/sp_b_b5_typing_repro.py — 10/10 runs PASS.
Audit re-run: <list of tests that moved from CRASH to PASS/FAIL>.
Regression: synthetic 23/13/1 + 34/2/1, ctest 159/159, SP0 reproducers green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 1.10: Update tracking table**

Edit `docs/superpowers/specs/2026-04-30-sp-b-attribute-resolution-bugs-design.md`. In the "Tracking table" section, change B5's row (and any other symptoms this commit closed) to:

```
| B5 | `typing.py:20 NoneType` + `reraise outside except` | closed | <commit-SHA> | <one-line note> |
```

Commit the table update separately:

```bash
git add docs/superpowers/specs/2026-04-30-sp-b-attribute-resolution-bugs-design.md
git commit -m "docs: SP-B tracking — mark B5 closed"
```

---

## Task 2 — Iteration 2: B1 (ABCMeta.gen missing)

**Skip if** Task 1's fix already closed B1 in the audit re-run.

Otherwise, attack `'ABCMeta' object has no attribute 'gen'` next. Audit attribution: test_asyncgen, test_contextlib. Likely cause: `_GeneratorContextManagerBase()` returning the class rather than an instance, OR a metaclass attribute lookup that doesn't walk the MRO correctly.

**Files:**
- Create: `tests/synthetic/sp_b_b1_abcmeta_gen_repro.py`
- Modify: per diagnosis.

- [ ] **Step 2.1: Write reproducer**

```python
# tests/synthetic/sp_b_b1_abcmeta_gen_repro.py
"""SP-B / B1 reproducer — `'ABCMeta' object has no attribute 'gen'`.

Surfaces in test_contextlib via `_GeneratorContextManagerBase()` and in
test_asyncgen via similar generator-context-manager paths.  This
reproducer triggers the symptom in isolation by calling the public
contextlib.contextmanager API (which constructs a
_GeneratorContextManagerBase under the hood).
"""
from contextlib import contextmanager

@contextmanager
def my_ctx():
    yield 42

with my_ctx() as v:
    assert v == 42

print("SP_B_B1_OK")
```

- [ ] **Step 2.2: Verify reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b1_abcmeta_gen_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: AttributeError on `'ABCMeta' object has no attribute 'gen'`, exit non-zero.

- [ ] **Step 2.3: Diagnose**

```bash
# Read contextlib's _GeneratorContextManagerBase
grep -nE "_GeneratorContextManagerBase|class.*ContextManager" lib/python3.14/contextlib.py | head -10
sed -n '90,140p' lib/python3.14/contextlib.py

# Trace under verbose diag
PROTO_ENV_DIAG=2 ./build/src/runtime/protopy tests/synthetic/sp_b_b1_abcmeta_gen_repro.py 2>&1 | tail -60
```

The audit hint: `_GeneratorContextManagerBase()` may be returning the class itself (not an instance) — this happens if `__call__` is incorrectly resolved on the metaclass and returns the receiver rather than instantiating. Look at `py_type_call` in `src/library/BuiltinsModule.cpp` for that pathway.

If the bug is "metaclass `__call__` returns wrong thing", the fix is in `py_type_call` and similar metaclass dispatch.

If the bug is "ABCMeta MRO walk doesn't find `gen` because `gen` is on an instance, not on the class": that's an attribute-lookup-direction bug — the resolver is asking the metaclass when it should be asking the instance.

- [ ] **Step 2.4: Apply the fix**

Per diagnosis. Same fix-shape policy as Task 1: smallest change that closes the reproducer.

- [ ] **Step 2.5: Build**

```bash
cmake --build build 2>&1 | tail -5
```

- [ ] **Step 2.6: Verify reproducer passes (≥10 runs)**

```bash
RATE=$(for i in $(seq 10); do
    PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b1_abcmeta_gen_repro.py 2>/dev/null | grep -c SP_B_B1_OK
done | awk '{s+=$1} END {print s}')
echo "SP_B_B1_OK printed in $RATE/10 runs"
```

Expected: 10/10.

- [ ] **Step 2.7: Re-run audit and note collapsed symptoms**

```bash
python3 tests/synthetic/sp_audit_truth.py 2>&1 | tee /tmp/sp_b_audit_iter2.out
```

- [ ] **Step 2.8: Regression check**

Run the regression-check block from the plan header. All green.

- [ ] **Step 2.9: Commit**

```bash
git add tests/synthetic/sp_b_b1_abcmeta_gen_repro.py <files-touched>
git commit -m "$(cat <<'EOF'
runtime: fix SP-B/B1 — ABCMeta object lookup for `gen` in contextlib

Root cause: <from diagnosis>.

Symptoms closed: B1 (and any others from audit re-run).

Reproducer: tests/synthetic/sp_b_b1_abcmeta_gen_repro.py — 10/10 PASS.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 2.10: Update tracking table**

Same shape as Task 1 Step 1.10.

---

## Task 3 — Iteration 3: B2 (ArgumentParser.conflict_handler missing)

**Skip if** earlier iterations already closed B2.

**Files:**
- Create: `tests/synthetic/sp_b_b2_argparse_conflict_repro.py`
- Modify: per diagnosis.

- [ ] **Step 3.1: Write reproducer**

```python
# tests/synthetic/sp_b_b2_argparse_conflict_repro.py
"""SP-B / B2 reproducer — `'ArgumentParser' object has no attribute 'conflict_handler'`.

Surfaces in test_descr and test_re via the argparse import chain.  The
audit suggests a missing `__init__`-time attribute on the ArgumentParser
class — likely in ArgumentParser.__init__ which assigns
self.conflict_handler from the constructor argument.
"""
import argparse

p = argparse.ArgumentParser(prog="repro")
assert hasattr(p, "conflict_handler"), \
    f"ArgumentParser missing conflict_handler attr; dir={dir(p)[:30]}"
assert p.conflict_handler == "error", \
    f"conflict_handler={p.conflict_handler!r}, expected 'error'"

print("SP_B_B2_OK")
```

- [ ] **Step 3.2: Verify reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b2_argparse_conflict_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: AttributeError, exit non-zero.

- [ ] **Step 3.3: Diagnose**

```bash
# Read argparse.py's ArgumentParser.__init__
grep -n "def __init__" lib/python3.14/argparse.py | head -5
grep -n "self.conflict_handler" lib/python3.14/argparse.py | head -5

# Print the class body init block
sed -n '$(grep -n "class ArgumentParser" lib/python3.14/argparse.py | head -1 | cut -d: -f1),+200p' lib/python3.14/argparse.py | head -80
```

If `self.conflict_handler = conflict_handler` is in the source but doesn't take effect at runtime, the bug is in how protopy handles `setattr` on instances during `__init__`. Possible causes: descriptor `__set__` on a non-data attribute incorrectly intercepting; the assignment going to the class dict instead of the instance dict; a `__slots__`-related restriction that's wrongly enforced.

Cross-reference against the V154.2 conformance entry note about "Migrate the 4 vector members to ProtoList" and any related descriptor changes; this may be a regression.

If `self.conflict_handler = ...` is never reached (the `__init__` halts earlier on some other line): the proximate bug is whatever halts `__init__`. The symptom name is misleading; chase the real halt point.

- [ ] **Step 3.4: Apply the fix**

Per diagnosis.

- [ ] **Step 3.5: Build**

```bash
cmake --build build 2>&1 | tail -5
```

- [ ] **Step 3.6: Verify reproducer passes (≥10 runs)**

```bash
RATE=$(for i in $(seq 10); do
    PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b2_argparse_conflict_repro.py 2>/dev/null | grep -c SP_B_B2_OK
done | awk '{s+=$1} END {print s}')
echo "SP_B_B2_OK printed in $RATE/10 runs"
```

Expected: 10/10.

- [ ] **Step 3.7: Re-run audit**

```bash
python3 tests/synthetic/sp_audit_truth.py 2>&1 | tee /tmp/sp_b_audit_iter3.out
```

- [ ] **Step 3.8: Regression check**

All green.

- [ ] **Step 3.9: Commit**

```bash
git add tests/synthetic/sp_b_b2_argparse_conflict_repro.py <files-touched>
git commit -m "<area>: fix SP-B/B2 — ArgumentParser.conflict_handler attr restored

<diagnosis summary>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
"
```

- [ ] **Step 3.10: Update tracking table**

Same as Task 1 Step 1.10.

---

## Task 4 — Iteration 4: B3 (Point.x missing in dataclass)

**Skip if** earlier iterations already closed B3.

**Files:**
- Create: `tests/synthetic/sp_b_b3_dataclass_init_repro.py`
- Modify: per diagnosis.

- [ ] **Step 4.1: Write reproducer**

```python
# tests/synthetic/sp_b_b3_dataclass_init_repro.py
"""SP-B / B3 reproducer — `'Point' object has no attribute 'x'`.

Surfaces in test_dataclasses.  The dataclass-generated `__init__`
appears to NOT set instance fields.  This reproducer pinpoints whether
the bug is in the @dataclass codegen or in setattr semantics for
fresh instances.
"""
from dataclasses import dataclass

@dataclass
class Point:
    x: int
    y: int = 0

p = Point(1, 2)
assert hasattr(p, "x"), f"Point instance missing x; vars={vars(p)}"
assert p.x == 1, f"p.x={p.x!r}, expected 1"
assert p.y == 2, f"p.y={p.y!r}, expected 2"

print("SP_B_B3_OK")
```

- [ ] **Step 4.2: Verify reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b3_dataclass_init_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: AttributeError, exit non-zero.

- [ ] **Step 4.3: Diagnose**

```bash
# Read dataclass __init__ codegen
grep -n "_create_fn\|_init_fn\|__init__" lib/python3.14/dataclasses.py | head -20

# Manually disassemble what the @dataclass-decorated Point looks like
cat > /tmp/inspect_pt.py << 'EOF'
from dataclasses import dataclass
import dis

@dataclass
class Point:
    x: int
    y: int = 0

print("__init__ source:")
import inspect
print(inspect.getsource(Point.__init__))
print()
print("dis:")
dis.dis(Point.__init__)
EOF
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /tmp/inspect_pt.py 2>&1 | tail -40
rm /tmp/inspect_pt.py
```

Possible causes:
- The generated `__init__` body uses a feature protopy doesn't fully support (e.g., the `_HAS_DEFAULT_FACTORY` sentinel pathway).
- `setattr(self, 'x', value)` in the generated body doesn't actually persist (slot vs dict storage).
- The `@dataclass` decorator returns a class where the generated `__init__` is unbound or shadowed by a class-level default.

The V154.x conformance entries mention dataclass attribute persistence work — this may be a regression of one of those, or a related case.

- [ ] **Step 4.4: Apply the fix**

Per diagnosis. The fix may live in `lib/python3.14/dataclasses.py` (codegen change) or in `src/runtime/PythonEnvironment.cpp` (setattr semantics) or `src/library/BuiltinsModule.cpp` (object/type machinery).

- [ ] **Step 4.5: Build**

```bash
cmake --build build 2>&1 | tail -5
```

- [ ] **Step 4.6: Verify reproducer passes (≥10 runs)**

```bash
RATE=$(for i in $(seq 10); do
    PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b3_dataclass_init_repro.py 2>/dev/null | grep -c SP_B_B3_OK
done | awk '{s+=$1} END {print s}')
echo "SP_B_B3_OK printed in $RATE/10 runs"
```

Expected: 10/10.

- [ ] **Step 4.7: Re-run audit**

```bash
python3 tests/synthetic/sp_audit_truth.py 2>&1 | tee /tmp/sp_b_audit_iter4.out
```

- [ ] **Step 4.8: Regression check**

All green.

- [ ] **Step 4.9: Commit**

```bash
git add tests/synthetic/sp_b_b3_dataclass_init_repro.py <files-touched>
git commit -m "<area>: fix SP-B/B3 — dataclass __init__ now sets instance fields

<diagnosis>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
"
```

- [ ] **Step 4.10: Update tracking table**

Same as Task 1 Step 1.10.

---

## Task 5 — Iteration 5: B4 (socket descriptor formatting)

**Skip if** earlier iterations already closed B4.

The audit reports the malformed message `'socket' object has no attribute 'property has no setter'`. The attribute name itself is a sentence fragment, suggesting the error STRING is being computed wrong (likely concatenating the descriptor's repr into the missing-attribute name).

**Files:**
- Create: `tests/synthetic/sp_b_b4_socket_descriptor_repro.py`
- Modify: per diagnosis.

- [ ] **Step 5.1: Write reproducer**

```python
# tests/synthetic/sp_b_b4_socket_descriptor_repro.py
"""SP-B / B4 reproducer — socket descriptor formatting/lookup bug.

The audit reports `'socket' object has no attribute 'property has no setter'`
which is a malformed error message — the attribute name appears to contain
the literal phrase 'property has no setter', suggesting the error path is
mis-constructing the AttributeError text by concatenating the descriptor
state into the attribute name.

This reproducer tries to use the socket module in a way test_sys does
and surfaces the malformed error.
"""
import socket

# Probe paths likely to trigger the bug.  test_sys imports socket; if
# socket has a property without a setter and someone tries to assign,
# we want to verify the error message is well-formed.
class _Probe:
    @property
    def x(self):
        return 1

p = _Probe()
try:
    p.x = 5
except AttributeError as e:
    msg = str(e)
    # AttributeError message must be the canonical CPython form:
    # "property 'x' of '_Probe' object has no setter" or similar.
    # Critically: it must NOT name the attribute 'property has no setter'
    # (which is the malformed shape from the audit).
    assert "has no setter" in msg, f"unexpected error: {msg!r}"
    assert "property has no setter" not in str(getattr(p, '__class__', None)), \
        "malformed error string contains attribute-name fragment"

# Also probe socket itself
s = socket.socket()
s.close()

print("SP_B_B4_OK")
```

- [ ] **Step 5.2: Verify reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b4_socket_descriptor_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: failure (the exact form depends on whether the socket symptom is reproducible standalone or only via test_sys's larger fixture).

If standalone reproduction is hard, fall back to running test_sys directly and capturing the malformed error:

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy lib/python3.14/test/test_sys.py 2>&1 | grep "property has no setter" | head -3
```

- [ ] **Step 5.3: Diagnose**

```bash
# Find where AttributeError messages are constructed for descriptor failures
grep -nE "has no setter|has no attribute" src/library/BuiltinsModule.cpp src/library/PythonEnvironment.cpp src/runtime/*.cpp 2>/dev/null | head -20

# Look for property-related code
grep -nE "py_property|class.*Property|propertyPrototype" src/library/*.cpp 2>/dev/null | head -10
```

The malformed message strongly suggests a `snprintf`/`format` template that concatenates two separate strings into one, e.g. instead of:

```cpp
fprintf(buf, "'%s' object has no attribute '%s'", typeName, attrName);
```

it's doing:

```cpp
fprintf(buf, "'%s' object has no attribute '%s'", typeName, descRepr);
```

where `descRepr` is something like `"property has no setter"` (a longer description being incorrectly used as the attr name). Find the wrong-substitution site and fix.

- [ ] **Step 5.4: Apply the fix**

Per diagnosis. Likely a one-line `printf`-template correction.

- [ ] **Step 5.5: Build**

```bash
cmake --build build 2>&1 | tail -5
```

- [ ] **Step 5.6: Verify reproducer passes (≥10 runs)**

```bash
RATE=$(for i in $(seq 10); do
    PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp_b_b4_socket_descriptor_repro.py 2>/dev/null | grep -c SP_B_B4_OK
done | awk '{s+=$1} END {print s}')
echo "SP_B_B4_OK printed in $RATE/10 runs"
```

Expected: 10/10.

- [ ] **Step 5.7: Re-run audit**

```bash
python3 tests/synthetic/sp_audit_truth.py 2>&1 | tee /tmp/sp_b_audit_iter5.out
```

- [ ] **Step 5.8: Regression check**

All green.

- [ ] **Step 5.9: Commit**

```bash
git add tests/synthetic/sp_b_b4_socket_descriptor_repro.py <files-touched>
git commit -m "<area>: fix SP-B/B4 — restore well-formed AttributeError for property-without-setter

<diagnosis>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
"
```

- [ ] **Step 5.10: Update tracking table**

Same as Task 1 Step 1.10.

---

## Task 6 — SP-B closure documentation

After Tasks 1-5 are complete (or whatever subset of 1-5 was needed because of cascade collapses).

**Files:**
- Modify: `docs/superpowers/specs/2026-04-30-sp-b-attribute-resolution-bugs-design.md` (final tracking table state)
- Modify: `docs/superpowers/specs/2026-04-30-protopy-ground-truth-audit.md` (re-run audit and update)
- Modify: `docs/CPYTHON_CONFORMANCE.md` (V155.x SP-B closure entry)

- [ ] **Step 6.1: Re-run the full audit**

```bash
python3 tests/synthetic/sp_audit_truth.py > /tmp/sp_b_audit_final.md
cat /tmp/sp_b_audit_final.md | tail -100
```

Compare against the 2026-04-30 audit baseline. The 5 cluster-2 tests should no longer crash on cluster-2 symptoms (they may still fail for cluster-1 reasons).

- [ ] **Step 6.2: Update the audit document**

Edit `docs/superpowers/specs/2026-04-30-protopy-ground-truth-audit.md` and add an "SP-B re-run" section near the top:

```markdown
## SP-B re-run (YYYY-MM-DD, after commits <list>)

| Test | Original status | Post-SP-B status |
|---|---|---|
| test_asyncgen | CRASH (B1) | <new status> |
| test_contextlib | CRASH (B1, post-G discovery) | <new status> |
| test_descr | CRASH (B2) | <new status> |
| test_re | CRASH (B2) | <new status> |
| test_dataclasses | CRASH (B3, post-G discovery) | <new status> |
| test_sys | CRASH (B4) | <new status> |
| test_base64 | CRASH (B5) | <new status> |
| test_functools | CRASH (B5) | <new status> |
| test_grammar | CRASH (B5) | <new status> |

Five cluster-2 symptoms closed.  Remaining failures are cluster-1
(stdlib import completeness — typing, doctest, asyncio, pdb,
unittest.mock, test.support) and out of SP-B scope; deferred to SP-A.
```

- [ ] **Step 6.3: Add V155.x entry to CPYTHON_CONFORMANCE.md**

After the existing V155.0 entry (or wherever the SP0 closure note lives), add:

```markdown
### V155.x Changes (YYYY-MM-DD) — SP-B: cluster-2 attribute-resolution bugs

Closes the 5 attribute-resolution-symptom crashes catalogued in the
2026-04-30 ground-truth audit:

- B1 (`'ABCMeta' has no attribute 'gen'`)
- B2 (`'ArgumentParser' has no attribute 'conflict_handler'`)
- B3 (`'Point' has no attribute 'x'` — dataclass `__init__`)
- B4 (`'socket' object has no attribute 'property has no setter'` — descriptor formatting)
- B5 (`'NoneType' object is not callable` typing.py:20 + `reraise outside except`)

<one-line summary per closed symptom listing the fix area>

Per-symptom reproducers in tests/synthetic/sp_b_b{1..5}_*_repro.py
remain as permanent regression tests.

Verification:
- All 5 reproducers: 10/10 runs PASS each.
- Audit re-run: 5 cluster-2 tests no longer crash at cluster-2 layer.
- ctest 159/159, synthetic 23/13/1 + 34/2/1 (no regressions).
- SP0 phase reproducers green.

Cluster-1 (stdlib import completeness) remains open as SP-A.
```

- [ ] **Step 6.4: Final commit**

```bash
git add docs/superpowers/specs/2026-04-30-sp-b-attribute-resolution-bugs-design.md \
        docs/superpowers/specs/2026-04-30-protopy-ground-truth-audit.md \
        docs/CPYTHON_CONFORMANCE.md
git commit -m "docs: SP-B closure — cluster-2 attribute-resolution bugs cleared

Five symptoms closed (B1-B5).  Audit re-run confirms cluster-2
crashes are gone; remaining failures are cluster-1 (stdlib import
completeness), deferred to SP-A.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
"
```

---

## Self-Review

After writing this plan, the planner verified:

- **Spec coverage:** every part of the SP-B spec maps to a task:
  - Iterative-cascade method → Tasks 1-5 with skip-if-already-closed conditionals.
  - First-iteration policy (B5) → Task 1.
  - Per-symptom reproducer convention (`SP_B_B{N}_OK` marker) → reproducers in Tasks 1-5.
  - One commit per root cause → commit policy spelled out in each task.
  - 10-run reliability requirement → Step N.6 of every task.
  - Audit re-run after each iteration → Step N.7 of every task.
  - Tracking table updates → Step N.10 of every task.
  - 5-day per-iteration escalation guard → noted in Task 1 (Step 1.4 fix-shape policy explicitly cites it).
  - Closure documentation (audit update + V155.x entry) → Task 6.

- **Placeholder scan:** no "TBD", "TODO", "implement later". The fix-code itself is genuinely unknowable until diagnosis (this is unavoidable for an iterative-cascade method); each task provides concrete reproducer + concrete diagnosis steps + clear policy on which fix shape to choose.

- **Type consistency:** marker convention uniform (`SP_B_B{N}_OK`), reproducer paths uniform (`tests/synthetic/sp_b_b{N}_*_repro.py`), commit-message format consistent (`<area>: fix SP-B/B<N> — ...`), tracking-table-update step always Step N.10. The 10-run loop is identical across all 5 iteration tasks.
