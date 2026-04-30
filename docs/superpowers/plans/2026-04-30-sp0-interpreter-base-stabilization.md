# SP0 — Interpreter Base Stabilization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the six base-interpreter bugs that block SP1's audit infrastructure, and deliver a permanent regression smoke suite (`sp0_smoke.py`) that exercises the four stabilized subsystems.

**Architecture:** Five phases, ordered quick-wins-first: (1) DEBUG printf cleanup, (2) JSON encoder int unboxing, (3) sys.path runtime mutation, (4) exception machinery, (5) consolidated smoke suite. Each phase follows an 8-step TDD cycle (reproducer → verify-fails → diagnose → fix → build → verify-passes → regression check → commit). One commit per discrete bug; if a phase reveals N sub-bugs, N commits.

**Tech Stack:** protopy (C++ interpreter at `build/src/runtime/protopy`, sources in `src/runtime/`, `src/library/`), Python stdlib in `lib/python3.14/`, gdb for silent-abort diagnosis, cmake for builds.

**Spec reference:** `docs/superpowers/specs/2026-04-30-sp0-interpreter-base-stabilization-design.md`

---

## File Structure

| Path | Responsibility | Status |
|---|---|---|
| `tests/synthetic/sp0_phase1_repro.py` | Phase 1 reproducer (DEBUG stdout cleanliness) | new (Task 1) |
| `tests/synthetic/sp0_phase2_repro.py` | Phase 2 reproducer (json int) | new (Task 2) |
| `tests/synthetic/sp0_phase3_repro.py` | Phase 3 reproducer (sys.path) | new (Task 3) |
| `tests/synthetic/sp0_phase4_repro.py` | Phase 4 reproducer (exceptions) | new (Task 4) |
| `tests/synthetic/sp0_smoke.py` | Consolidated smoke driver | new (Task 5) |
| `src/runtime/*.cpp`, `src/library/*.cpp` | Per-phase fixes | modified (Tasks 1-4) |
| `lib/python3.14/json/encoder.py` | Possible Python-side fix for Phase 2 | modified (Task 2 if applicable) |
| `lib/python3.14/traceback.py` | Possible Python-side fix for Phase 4 | modified (Task 4 if applicable) |
| `tests/audits/_probe_lib.py` | Reverted from v2 (workaround) to v1 (idiomatic) | modified (Task 6) |
| `docs/superpowers/specs/2026-04-29-sp1-test-infra-audit-design.md` | PAUSED → Active | modified (Task 6) |
| `docs/CPYTHON_CONFORMANCE.md` | V155.0 entry for SP0 closure | modified (Task 6) |

**Build / run conventions** (apply to every test-execution step):

- Build: `cmake --build build 2>&1 | tail -5` from `protoPython/` root.
- Run a script: `PROTO_ENV_DIAG=0 ./build/src/runtime/protopy <script.py>`.
- protopy emits unconditional `DEBUG:` lines to stderr; in stdout-checking contexts use `2>/dev/null`.
- Phase 1 explicitly verifies stdout is DEBUG-free, so we do **not** filter stdout in Phase 1.

**Regression check** (used at Step 7 of every phase):

```bash
# Synthetic suite — must remain 37/0/0
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/test_generators_synthetic.py 2>&1 | tail -5
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/test_metaclass_descr_synthetic.py 2>&1 | tail -5

# Custom Necessary suites — must all PASS
for t in tests/test_decorator.py tests/test_abc.py tests/test_contextlib.py tests/test_dataclasses.py; do
    echo "=== $t ==="
    PROTO_ENV_DIAG=0 ./build/src/runtime/protopy "$t" 2>&1 | tail -3
done

# protoCore C++ tests — must remain 136/136
ctest --test-dir build -j$(nproc) --output-on-failure 2>&1 | tail -5
```

---

## Task 1 — Phase 1: DEBUG printf cleanup (bug C)

**Files:**
- Create: `tests/synthetic/sp0_phase1_repro.py`
- Modify: `src/runtime/*.cpp` and/or `src/library/*.cpp` (specific files identified by Step 3 grep)

- [ ] **Step 1.1: Write the reproducer**

```python
# tests/synthetic/sp0_phase1_repro.py
"""Phase 1 reproducer — DEBUG stdout cleanliness.

Importing a local module currently emits ~240 lines of "DEBUG:"
output on stdout (not stderr), pollluting the stdout of any
script that imports anything.  This script triggers the leak via
a benign local import and prints a marker.  Driver checks: stdout
must contain MARKER_PHASE1 AND must NOT contain any "DEBUG:" lines.
"""
import os
print("MARKER_PHASE1", flush=True)
```

- [ ] **Step 1.2: Verify the reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp0_phase1_repro.py 2>/dev/null > /tmp/sp0_p1.stdout
# Must contain marker:
grep -q "MARKER_PHASE1" /tmp/sp0_p1.stdout && echo "marker: OK" || echo "marker: MISSING"
# Must NOT contain DEBUG lines:
DEBUG_COUNT=$(grep -c "^DEBUG:" /tmp/sp0_p1.stdout)
echo "DEBUG lines on stdout: $DEBUG_COUNT (must be 0 to pass)"
```

Expected on current protopy: marker OK, DEBUG_COUNT > 0 → reproducer FAILS as designed.

- [ ] **Step 1.3: Diagnose — locate the unconditional stdout writes**

```bash
# Search for unconditional stdout-bound prints in interpreter sources.
# stderr-bound prints (fprintf(stderr, ...), std::cerr) are fine.
grep -nE 'fprintf\(stdout|std::cout|printf\("DEBUG' src/runtime/*.cpp src/library/*.cpp 2>/dev/null
# Also search for printf("DEBUG ...) without a stream argument (printf goes to stdout):
grep -nE 'printf\(' src/runtime/*.cpp src/library/*.cpp 2>/dev/null | grep -v "fprintf" | grep -i "debug"
```

Expected: a list of source locations. For each location, decide:
- Genuinely unconditional? → Either remove or gate behind `PROTO_ENV_DIAG`.
- Already gated? → Verify the gate condition reads the env var correctly.

Cross-reference against the actual DEBUG lines emitted by Step 1.2 — match the prefix string in each DEBUG line back to a `printf` site.

- [ ] **Step 1.4: Apply the fix**

For each unconditional `printf("DEBUG:...")`, choose:

(a) If the message has no diagnostic value any more, **delete** it.

(b) If it has value but only for debugging, **gate** behind the existing env-var pattern. The codebase already uses `get_env_diag()` from `include/protoPython/DiagUtils.h` (per `CPYTHON_CONFORMANCE.md` V97). Apply this pattern:

```cpp
// Before:
printf("DEBUG: py_type_call called self=%p\n", self);

// After (gate behind PROTO_ENV_DIAG):
#include "DiagUtils.h"
// ...
if (get_env_diag()) {
    fprintf(stderr, "DEBUG: py_type_call called self=%p\n", self);
}
```

Note the change from `printf` (stdout) to `fprintf(stderr, ...)`. Even when the gate is on, diagnostic output belongs on stderr.

- [ ] **Step 1.5: Build**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: clean build, no new warnings.

- [ ] **Step 1.6: Verify the reproducer passes**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp0_phase1_repro.py 2>/dev/null > /tmp/sp0_p1.stdout
grep -q "MARKER_PHASE1" /tmp/sp0_p1.stdout && echo "marker: OK" || (echo "marker: MISSING"; exit 1)
DEBUG_COUNT=$(grep -c "^DEBUG:" /tmp/sp0_p1.stdout)
[ "$DEBUG_COUNT" -eq 0 ] && echo "DEBUG-free: OK" || (echo "DEBUG_COUNT=$DEBUG_COUNT (expected 0)"; exit 1)
```

Both checks must succeed.

- [ ] **Step 1.7: Regression check**

Run the regression check block from the plan header. All of: synthetic suite 37/0/0, custom Necessary suites all PASS, protoCore tests 136/136.

- [ ] **Step 1.8: Commit**

```bash
git add tests/synthetic/sp0_phase1_repro.py src/runtime/*.cpp src/library/*.cpp
git commit -m "$(cat <<'EOF'
runtime: fix SP0-P1 — gate or remove unconditional DEBUG prints to stdout

Local module imports leaked ~240 lines of "DEBUG:" output on stdout,
contaminating any script that imports anything.  All offending sites
gated behind PROTO_ENV_DIAG via get_env_diag() and routed to stderr.

Reproducer: tests/synthetic/sp0_phase1_repro.py — must run with no
"DEBUG:" lines on stdout and must print MARKER_PHASE1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

If the diagnosis revealed multiple discrete sites that warrant separate commits, split into N commits each with its own SP0-P1.x suffix.

---

## Task 2 — Phase 2: JSON encoder int unboxing (bug A)

**Files:**
- Create: `tests/synthetic/sp0_phase2_repro.py`
- Diagnose / Modify: `lib/python3.14/json/encoder.py` and possibly C++ helpers in `src/library/`.

- [ ] **Step 2.1: Write the reproducer**

```python
# tests/synthetic/sp0_phase2_repro.py
"""Phase 2 reproducer — json.dumps must encode integers correctly.

Currently json.dumps(5) returns "<NoneType object at (nil)>" instead
of "5"; strings work fine.  This reproducer asserts canonical JSON
encoding for several int-bearing shapes.
"""
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

- [ ] **Step 2.2: Verify the reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp0_phase2_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: AssertionError on the first assert, exit non-zero.

- [ ] **Step 2.3: Diagnose**

```bash
# Read the json encoder
sed -n '1,60p' lib/python3.14/json/encoder.py | head -60
# Find the int handler — typically in iterencode or in the C-accelerated path
grep -n "int\|integer\|isinstance" lib/python3.14/json/encoder.py | head -20
# Check whether protopy's json module has a native shortcut
grep -nE "json|JSON" src/library/*.cpp 2>/dev/null | head -10
# Compare against repr(int) which already works:
echo 'print(repr(5))' | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>/dev/null
```

Expected findings:
- The encoder's iterencode dispatches on type. For `int`, CPython emits via `_iterencode_intc` (C-accelerated) or `int.__repr__`.
- protopy may register a different int-handler that returns wrong values.
- The bug location is whichever helper takes a SmallInteger tagged-pointer and produces the string representation for JSON.

- [ ] **Step 2.4: Apply the fix**

The fix path depends on the diagnosis. The most likely shape: the int-handler in `lib/python3.14/json/encoder.py` calls a function that does not properly unbox SmallInteger. Possible patterns:

(a) **Python-side fix**: replace the int-encoding call with `str(o)` or `repr(o)` (both already work for ints):

```python
# In lib/python3.14/json/encoder.py, locate the iterencode or _make_iterencode
# function and the int branch. Replace any call that returns wrong output for
# int with a str() call, e.g.:
#
# Before (hypothetical):
#     yield int_to_json(o)  # returns "<NoneType object at (nil)>"
# After:
#     yield str(o)
```

(b) **C++-side fix**: if a native int-to-string helper is registered, fix the unboxing path to handle SmallInteger tagged pointers properly. Look in `src/library/JsonModule.cpp` (if present) or `src/library/BuiltinsModule.cpp`.

- [ ] **Step 2.5: Build**

```bash
cmake --build build 2>&1 | tail -5
```

- [ ] **Step 2.6: Verify the reproducer passes**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp0_phase2_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: prints `PHASE2_OK`, exit 0.

If Phase 2 also reveals that other types (float, bool, None) are similarly broken in json.dumps, fix them in this phase as well — extend the reproducer with the additional asserts and add their commits.

- [ ] **Step 2.7: Regression check**

Run the regression check block from the plan header. Pay special attention to `test_json.py` — the conformance doc claims it's 9/9 PASS, but those tests apparently don't cover int dumps. After the Phase 2 fix, re-run `test_json.py` to confirm it still passes (it should, and ideally improves):

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy lib/python3.14/test/test_json.py 2>&1 | tail -5
```

- [ ] **Step 2.8: Commit**

```bash
git add tests/synthetic/sp0_phase2_repro.py lib/python3.14/json/ src/library/
git commit -m "$(cat <<'EOF'
json: fix SP0-P2 — encoder properly unboxes SmallInteger int values

json.dumps(5) used to return "<NoneType object at (nil)>" because the
encoder's int-handler did not unbox SmallInteger tagged pointers; only
the string-handler path was correct.  Routed int encoding through a
known-good repr/str path.

Reproducer: tests/synthetic/sp0_phase2_repro.py asserts canonical JSON
output for int, dict-with-int, list-of-int, and nested mix.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3 — Phase 3: sys.path runtime mutation (bug B)

**Files:**
- Create: `tests/synthetic/sp0_phase3_repro.py`
- Modify: `src/runtime/ImpModule.cpp` (most likely) — the function that resolves a module name to a file path.

- [ ] **Step 3.1: Write the reproducer**

```python
# tests/synthetic/sp0_phase3_repro.py
"""Phase 3 reproducer — sys.path runtime mutation must affect import.

sys.path.insert(0, p) updates sys.path but the import resolver does
not consult the live list; it appears to use a startup snapshot.
This reproducer creates a temp file in a fresh directory, prepends
that directory to sys.path, then asserts the module can be imported.
"""
import sys, os, tempfile

d = tempfile.mkdtemp(prefix="sp0_p3_")
mod_path = os.path.join(d, "_sp0_p3_temp.py")
with open(mod_path, "w") as f:
    f.write('def hello(): return "ok"\n')

sys.path.insert(0, d)
import _sp0_p3_temp
assert _sp0_p3_temp.hello() == "ok", \
    f"hello() returned {_sp0_p3_temp.hello()!r}"

# Cleanup
import shutil
shutil.rmtree(d, ignore_errors=True)
print("PHASE3_OK")
```

- [ ] **Step 3.2: Verify the reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp0_phase3_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: `ModuleNotFoundError: No module named '_sp0_p3_temp'`, exit non-zero.

- [ ] **Step 3.3: Diagnose**

```bash
# Find the module resolution function
grep -nE "resolveModule|importModule|searchPath|sys.*path" src/runtime/ImpModule.cpp 2>/dev/null | head -20
# Also check if there's a snapshot variable
grep -nE "sysPath_|stdLibPath|moduleSearchPath" src/runtime/*.cpp src/runtime/*.h 2>/dev/null | head -20
```

Expected pattern: the resolver iterates over a C++-side `std::vector<std::string>` or similar that was populated at startup from `sys.path`. The fix is to consult the live `sys.modules['sys'].path` (which IS reachable via the Python object model) on every resolve.

- [ ] **Step 3.4: Apply the fix**

The fix is to replace the snapshot lookup with a live read of `sys.path`. Pattern in C++:

```cpp
// In ImpModule.cpp resolveModule (or equivalent):
//
// 1. Get the sys module from the environment.
// 2. Get its 'path' attribute (a ProtoList).
// 3. Iterate over the live list to find the module file.
//
// Pseudocode:
const proto::ProtoObject* sysModule = env->resolveModule("sys", ctx);
const proto::ProtoString* pathKey =
    proto::ProtoString::createSymbol(ctx, "path");
const proto::ProtoObject* pathList =
    sysModule->getAttribute(ctx, pathKey, false);
if (pathList && pathList->isList(ctx)) {
    const proto::ProtoList* live = pathList->asList(ctx);
    long n = live->getSize(ctx);
    for (long i = 0; i < n; ++i) {
        const proto::ProtoObject* entry = live->getAt(ctx, i);
        if (!entry || !entry->isString(ctx)) continue;
        std::string dir; entry->asString(ctx)->toUTF8String(ctx, dir);
        // try dir + "/" + moduleName + ".py"
        // ... (rest of the resolution)
    }
}
```

The exact API names match other `ProtoObject` consumers in the same file. Read sibling functions in `ImpModule.cpp` for the exact form.

- [ ] **Step 3.5: Build**

```bash
cmake --build build 2>&1 | tail -5
```

- [ ] **Step 3.6: Verify the reproducer passes**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp0_phase3_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: prints `PHASE3_OK`, exit 0.

- [ ] **Step 3.7: Regression check**

Run the regression check block from the plan header. Pay special attention to import-heavy tests:

```bash
# enum, importlib, abc all import heavily; they must remain working
echo 'import enum; print(enum.Enum)' | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>/dev/null
echo 'import importlib; print(importlib.__name__)' | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>/dev/null
echo 'import abc; print(abc.ABC)' | PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /dev/stdin 2>/dev/null
```

Expected: all three import without error.

- [ ] **Step 3.8: Commit**

```bash
git add tests/synthetic/sp0_phase3_repro.py src/runtime/ImpModule.cpp
git commit -m "$(cat <<'EOF'
runtime: fix SP0-P3 — module resolver consults live sys.path, not snapshot

resolveModule used to iterate over a startup snapshot of sys.path;
sys.path.insert(0, ...) at runtime never affected import resolution.
The resolver now reads sys.modules['sys'].path on every call.

Reproducer: tests/synthetic/sp0_phase3_repro.py creates a temp module,
prepends its directory to sys.path, and imports it.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4 — Phase 4: Exception machinery (bugs D, E, F)

**Files:**
- Create: `tests/synthetic/sp0_phase4_repro.py`
- Modify: `src/runtime/ExecutionEngine.cpp`, `src/runtime/Compiler.cpp`, `src/runtime/PythonEnvironment.cpp` (some subset; depends on diagnosis); possibly `lib/python3.14/traceback.py`.

This phase has up to four sub-bugs (4a, 4b, 4c, 4d) that may share a single root cause or be independent. The plan walks all four; commits separately if the bugs are independent, jointly if the diagnosis reveals one root cause.

- [ ] **Step 4.1: Write the reproducer**

```python
# tests/synthetic/sp0_phase4_repro.py
"""Phase 4 reproducer — exception machinery.

Four sub-checks; any failure indicates the corresponding sub-bug.

4a: import traceback succeeds
4b: except Exception as e binds e correctly
4c: type(e).__name__ on a caught exception
4d: traceback.format_exc returns a string with type and message
"""
# 4a: import traceback
import traceback
assert hasattr(traceback, "format_exc"), \
    "traceback module imported but missing format_exc"

# 4b: except-as binding preserves args
try:
    raise ValueError("foo")
except Exception as e:
    assert e.args == ("foo",), f"e.args={e.args!r}"

# 4c: type(e).__name__ on caught exception
try:
    1 / 0
except BaseException as e:
    assert type(e).__name__ == "ZeroDivisionError", \
        f"type(e).__name__ = {type(e).__name__!r}"

# 4d: traceback.format_exc returns string with type and message
try:
    raise RuntimeError("trace test")
except Exception:
    s = traceback.format_exc()
    assert "RuntimeError" in s, f"format_exc missing type: {s!r}"
    assert "trace test" in s, f"format_exc missing message: {s!r}"

print("PHASE4_OK")
```

- [ ] **Step 4.2: Verify the reproducer fails**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp0_phase4_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: silent abort or AssertionError, exit non-zero.

If the script exits silently after only printing nothing, the failure mode is bug D (`import traceback` aborts). Use stderr to learn more:

```bash
PROTO_ENV_DIAG=2 ./build/src/runtime/protopy tests/synthetic/sp0_phase4_repro.py 2>&1 \
    | grep -E "DEBUG:|FATAL|exception|raise" | head -50
```

- [ ] **Step 4.3: Diagnose 4a (import traceback)**

```bash
# Read traceback.py to find what features it uses at import time
sed -n '1,80p' lib/python3.14/traceback.py
# Look for the failure point under PROTO_ENV_DIAG=2
PROTO_ENV_DIAG=2 ./build/src/runtime/protopy -c "import traceback; print('ok')" 2>&1 | tail -80
# If silent abort persists, run under gdb (requires build-debug binary)
echo 'import traceback' > /tmp/it.py
gdb --batch --args build-debug/src/runtime/protopy /tmp/it.py \
    -ex "run" -ex "bt" 2>&1 | tail -40
rm /tmp/it.py
```

Expected: identification of the failing opcode, native function, or unimplemented Python feature inside traceback.py. Common candidates:
- traceback.py uses `_recursive_repr` from `linecache` which uses some unsupported feature.
- traceback.py raises an exception class that's part of a hierarchy where Exception/BaseException is wired wrong.
- An import inside traceback.py (e.g., `import collections.abc`) hits a separate bug.

- [ ] **Step 4.4: Diagnose 4b/4c (except-as binding, type access)**

```bash
# Independent reproducer for 4b
cat > /tmp/p4b.py << 'EOF'
try:
    raise ValueError("foo")
except Exception as e:
    print(repr(e))
    print("e.args:", e.args)
EOF
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /tmp/p4b.py 2>&1 | tail -10
rm /tmp/p4b.py

# Independent reproducer for 4c
cat > /tmp/p4c.py << 'EOF'
try:
    1/0
except BaseException as e:
    t = type(e)
    print("type:", t)
    print("type.__name__:", t.__name__)
EOF
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy /tmp/p4c.py 2>&1 | tail -10
rm /tmp/p4c.py
```

Expected findings — common patterns:
- 4b: `STORE_FAST` for `e` after `OP_PUSH_EXC_INFO` may be misimplemented in `ExecutionEngine.cpp`. The exception object is on the stack but the bind opcode pops the wrong item.
- 4c: descriptor protocol on type objects may fail when called from inside an except handler (frame state issue).

Cross-reference against `ExecutionEngine.cpp` opcodes `OP_PUSH_EXC_INFO`, `OP_RAISE_VARARGS`, `OP_LOAD_ATTR`, and `Compiler.cpp` `compileExcept`.

- [ ] **Step 4.5: Apply the fixes (one commit per discrete bug)**

For each sub-bug independently confirmed and isolated:

**4a fix (import traceback):** depends on diagnosis. If a Python-level feature is missing, either polyfill in `lib/python3.14/traceback.py` or implement the feature in C++. If the fix is one specific helper that traceback.py needs, isolate it.

**4b fix (except-as binding):** typical fix shape in `ExecutionEngine.cpp` op dispatch:

```cpp
// In OP_STORE_FAST handler immediately after exception is captured:
//
// Verify the value being stored is the exception object itself,
// not the type or the traceback or some adjacent stack item.
// CPython 3.11+ pushes [type, value, traceback] in that order;
// except-as binds to value (slot 1).
```

Read sibling `compileExcept` in `Compiler.cpp` and the corresponding op handler in `ExecutionEngine.cpp` for the exact form.

**4c fix (type(e).__name__):** typically a frame-attribute-resolution issue inside except handlers; verify `type()` returns the actual class rather than a proxy, and that `.__name__` resolves through the class's own attribute path (not the instance's).

**4d fix (traceback.format_exc):** likely cascades from 4a; once import traceback works, verify that 4d also passes. If not, separate fix.

- [ ] **Step 4.6: Build (after each fix)**

```bash
cmake --build build 2>&1 | tail -5
```

- [ ] **Step 4.7: Verify the reproducer passes**

```bash
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/synthetic/sp0_phase4_repro.py 2>/dev/null
echo "Exit: $?"
```

Expected: prints `PHASE4_OK`, exit 0.

- [ ] **Step 4.8: Regression check**

Run the regression check block from the plan header. Particularly important here — exception handling is pervasive:

```bash
# Try/except heavy workloads
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/test_contextlib.py 2>&1 | tail -3
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/test_decorator.py 2>&1 | tail -3
# Conformance test_json depends on exception handling for parse errors:
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy lib/python3.14/test/test_json.py 2>&1 | tail -5
```

- [ ] **Step 4.9: Commit (per discrete bug)**

For each isolated sub-bug, one commit. Example for 4a alone:

```bash
git add tests/synthetic/sp0_phase4_repro.py src/runtime/<file> lib/python3.14/traceback.py
git commit -m "$(cat <<'EOF'
runtime: fix SP0-P4a — import traceback no longer silently aborts

<diagnosis summary in 2-3 sentences>

Reproducer: 4a sub-check in tests/synthetic/sp0_phase4_repro.py.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

If 4a/4b/4c/4d turn out to share a single root cause, a single combined commit is acceptable; the message must explicitly identify the root cause and which sub-bugs it fixes.

**Phase-expansion guard:** if Phase 4 wall-clock exceeds 5 days without convergence, escalate to user before continuing. The escalation may produce a sub-decision: split into a new phase, accept the wall-clock, or revert to minimal scope.

---

## Task 5 — Phase 5: Smoke suite consolidation

**Files:**
- Create: `tests/synthetic/sp0_smoke.py`

- [ ] **Step 5.1: Write the smoke driver**

```python
# tests/synthetic/sp0_smoke.py
"""SP0 regression smoke — exercises the four stabilized subsystems.

Runs all four phase reproducers in sequence; if any fails the entire
suite fails. Designed to be cheap (~1s wall-clock) so it can run on
every commit touching the interpreter.
"""
import subprocess
import sys
import os

PHASES = [
    ("tests/synthetic/sp0_phase1_repro.py", "MARKER_PHASE1"),
    ("tests/synthetic/sp0_phase2_repro.py", "PHASE2_OK"),
    ("tests/synthetic/sp0_phase3_repro.py", "PHASE3_OK"),
    ("tests/synthetic/sp0_phase4_repro.py", "PHASE4_OK"),
]
PROTOPY = os.environ.get("PROTOPY", "./build/src/runtime/protopy")

failed = []
for phase_path, marker in PHASES:
    print(f"=== {phase_path} ===")
    env = dict(os.environ)
    env["PROTO_ENV_DIAG"] = "0"
    p = subprocess.run([PROTOPY, phase_path],
                       capture_output=True, text=True, env=env)
    has_debug_stdout = any(
        l.startswith("DEBUG:") for l in p.stdout.splitlines())
    has_marker = marker in p.stdout
    ok = (p.returncode == 0 and has_marker and not has_debug_stdout)
    # Show last 500 bytes of stdout for context
    print(p.stdout[-500:] if p.stdout else "(no stdout)")
    if not ok:
        reasons = []
        if p.returncode != 0:
            reasons.append(f"exit={p.returncode}")
        if not has_marker:
            reasons.append(f"missing marker {marker!r}")
        if has_debug_stdout:
            reasons.append("DEBUG: lines on stdout")
        print(f"  FAIL: {', '.join(reasons)}")
        failed.append(phase_path)

if failed:
    print(f"\nFAILED phases: {failed}")
    sys.exit(1)
print("\nALL PHASES PASS — SP0 stabilization regression smoke green")
```

- [ ] **Step 5.2: Run the smoke**

```bash
python3 tests/synthetic/sp0_smoke.py
```

Expected: `ALL PHASES PASS — SP0 stabilization regression smoke green`. Exit 0.

If any phase fails, return to that phase's task and reopen.

- [ ] **Step 5.3: Commit**

```bash
git add tests/synthetic/sp0_smoke.py
git commit -m "$(cat <<'EOF'
tests: SP0 — consolidated smoke regression suite for stabilized subsystems

sp0_smoke.py runs the four phase reproducers (DEBUG stdout cleanliness,
json int encoding, sys.path runtime mutation, exception machinery) and
fails the build if any regress.  Cheap enough (~1s) to gate every
interpreter-touching commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6 — Bridge to SP1 + V155.0 documentation

**Files:**
- Modify: `tests/audits/_probe_lib.py` (revert v2 workaround content to v1 idiomatic shape)
- Modify: `docs/superpowers/specs/2026-04-29-sp1-test-infra-audit-design.md` (PAUSED → Active)
- Modify: `docs/CPYTHON_CONFORMANCE.md` (add V155.0 entry)

- [ ] **Step 6.1: Revert `_probe_lib.py` to v1 idiomatic shape**

The v2 file (commit `cff36064`) used string-only json.dumps as a workaround for SP0's bugs. With SP0 fixed, revert to the cleaner v1 shape:

```python
# tests/audits/_probe_lib.py
"""Shared helpers for SP1 audit probes.

Each probe imports `expect`, calls it many times, then emits the
collected gaps as JSON on stdout via `emit()`.
"""
import json
import traceback

_gaps = []


def expect(name, fn, validate=None):
    """Run fn(); record success/failure with full context.

    Args:
        name: stable identifier for the gap (e.g. "doctest.testmod_basic").
        fn: zero-arg callable that exercises the API.
        validate: optional callable(result) -> bool; if provided and returns
            False, the entry is recorded as FAIL with reason "validate".
    """
    try:
        result = fn()
    except Exception as e:
        _gaps.append({
            "id": name,
            "status": "FAIL",
            "exc_type": type(e).__name__,
            "exc_msg": str(e),
            "traceback": traceback.format_exc(),
        })
        return
    if validate is not None:
        try:
            ok = validate(result)
        except Exception as e:
            _gaps.append({
                "id": name,
                "status": "FAIL",
                "exc_type": "ValidateError",
                "exc_msg": "validate raised %s: %s" % (type(e).__name__, e),
                "traceback": traceback.format_exc(),
            })
            return
        if not ok:
            _gaps.append({
                "id": name,
                "status": "FAIL",
                "exc_type": "ValidateMismatch",
                "exc_msg": "result did not satisfy validator: %r" % (result,),
                "traceback": "",
            })
            return
    _gaps.append({
        "id": name,
        "status": "PASS",
        "result": repr(result)[:200],
    })


def emit(module_name):
    """Print collected gaps as JSON, suitable for redirecting to a file."""
    out = {
        "module": module_name,
        "total": len(_gaps),
        "passes": sum(1 for g in _gaps if g["status"] == "PASS"),
        "fails": sum(1 for g in _gaps if g["status"] == "FAIL"),
        "entries": _gaps,
    }
    print(json.dumps(out, indent=2))
```

- [ ] **Step 6.2: Verify the v1 library now works**

```bash
cd tests/audits
cat > _smoke_probe.py << 'EOF'
import sys
sys.path.insert(0, 'tests/audits')
from _probe_lib import expect, emit

expect("smoke.always_pass", lambda: 1 + 1, validate=lambda r: r == 2)
expect("smoke.always_fail", lambda: 1 / 0)
emit("smoke")
EOF
PROTO_ENV_DIAG=0 ../../build/src/runtime/protopy _smoke_probe.py 2>/dev/null
rm _smoke_probe.py
cd ../..
```

Expected: clean JSON output with `"module": "smoke"`, `"total": 2`, one PASS one FAIL. No stringified-only fields, no DEBUG noise.

- [ ] **Step 6.3: Reactivate SP1 spec**

Edit `docs/superpowers/specs/2026-04-29-sp1-test-infra-audit-design.md` and replace the line:

```
**Status:** PAUSED on 2026-04-29. ...
```

with:

```
**Status:** Active (resumed after SP0 closure on YYYY-MM-DD; commit <SP0-final-SHA>).

**Note:** SP1 had been paused while SP0 fixed six base-interpreter bugs that blocked the audit infrastructure. With SP0 closed, SP1 v2 workarounds in `tests/audits/_probe_lib.py` were reverted to the idiomatic v1 shape; SP1 Phase 1 resumes from Task 1.1.
```

(Replace `YYYY-MM-DD` with the actual date and `<SP0-final-SHA>` with the commit SHA from Task 5.3.)

- [ ] **Step 6.4: Add V155.0 entry to CPYTHON_CONFORMANCE.md**

After the existing "V154.9 Changes" block (line ~89 of the doc), add:

```markdown
### V155.0 Changes (YYYY-MM-DD) — SP0: interpreter base stabilization

Closes SP0, the prerequisite sub-project that fixes six base-
interpreter bugs surfaced when SP1 attempted to bootstrap its audit
infrastructure.  The bugs were: (1) DEBUG output leaking to stdout
on local imports, (2) json.dumps mis-encoding integers, (3) sys.path
runtime mutation not affecting import resolution, (4) import
traceback silently aborting, (5) except Exception not catching what
BaseException catches, (6) attribute access on caught exception
crashing.

**Phase 1** (DEBUG cleanup): unconditional printf("DEBUG:...") sites
gated behind PROTO_ENV_DIAG and routed to stderr.

**Phase 2** (json int): encoder int-handler now produces correct
"5" output instead of "<NoneType object at (nil)>".

**Phase 3** (sys.path): module resolver consults live
sys.modules['sys'].path on every resolve.

**Phase 4** (exceptions): import traceback works; except-as binding
preserves the exception object; type(e).__name__ no longer crashes;
traceback.format_exc returns the expected string.

**Smoke suite**: tests/synthetic/sp0_smoke.py runs all four phase
reproducers; permanent regression test.

**Verification**:
- All four reproducers PASS.
- sp0_smoke.py: ALL PHASES PASS.
- Synthetic suite: 37/0/0 (no regression).
- Custom Necessary suites: 4/4 PASS.
- protoCore tests: 136/136.

**SP1 unblocked**: tests/audits/_probe_lib.py reverted to v1
idiomatic shape; SP1 resumes from Phase 1 Task 1.1.
```

(Replace `YYYY-MM-DD` with the actual date.)

- [ ] **Step 6.5: Final regression sweep + commit**

```bash
# Full regression check
ctest --test-dir build -j$(nproc) --output-on-failure 2>&1 | tail -5
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/test_generators_synthetic.py 2>&1 | tail -3
PROTO_ENV_DIAG=0 ./build/src/runtime/protopy tests/test_metaclass_descr_synthetic.py 2>&1 | tail -3
for t in tests/test_decorator.py tests/test_abc.py tests/test_contextlib.py tests/test_dataclasses.py; do
    PROTO_ENV_DIAG=0 ./build/src/runtime/protopy "$t" 2>&1 | tail -3
done
python3 tests/synthetic/sp0_smoke.py
```

All must be green. Then commit:

```bash
git add tests/audits/_probe_lib.py \
        docs/superpowers/specs/2026-04-29-sp1-test-infra-audit-design.md \
        docs/CPYTHON_CONFORMANCE.md
git commit -m "$(cat <<'EOF'
docs: SP0 closure — revert SP1 _probe_lib to v1, reactivate SP1, V155.0 entry

SP0 (interpreter base stabilization) is closed with all four phases
green and the consolidated smoke suite passing.  This commit removes
the SP1 v2 workarounds and reactivates SP1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review

After writing this plan, the planner verified:

- **Spec coverage:** Every part of the SP0 spec maps to a task:
  - Phase 1 (DEBUG cleanup) → Task 1.
  - Phase 2 (json int) → Task 2.
  - Phase 3 (sys.path) → Task 3.
  - Phase 4 (exceptions, 3-4 sub-bugs) → Task 4 (single task; multiple commits if sub-bugs are independent).
  - Phase 5 (smoke consolidation) → Task 5.
  - Bridge to SP1 + V155.0 doc → Task 6.

- **Placeholder scan:** No "TBD", "TODO", or "implement later". Where the fix code is genuinely unknowable until diagnosis (Phase 4 sub-fixes), the plan provides the concrete diagnosis procedure and the typical fix shape rather than vague directions.

- **Type consistency:** Reproducer marker conventions are uniform: `MARKER_PHASE1`, `PHASE2_OK`, `PHASE3_OK`, `PHASE4_OK`. File paths use `tests/synthetic/sp0_phase{N}_repro.py` consistently. Commit message format uses `SP0-P{N}` consistently.

- **Phase-expansion handling:** Each phase's commit policy is "one commit per discrete bug; multiple commits if sub-bugs independent". Phase 4 has explicit escalation guard (5 days wall-clock).
