# Phase F0 Implementation Plan: Bignum API + `proto_internal.h` Removal

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend protoCore.h's public API with bignum-aware integer accessors (`ProtoObject::integerSign`, `ProtoObject::asIntegerString`) and remove every `#include <proto_internal.h>` from protoPython source code, without regressing any test that currently passes.

**Architecture:** Add two methods to `protoCore::ProtoObject` declared in `protoCore/headers/protoCore.h` and implemented in `protoCore/core/ProtoObject.cpp` as one-line delegators to the existing internal `Integer::sign` and `Integer::toString`. Then migrate `BuiltinsModule.cpp`, `ExecutionEngine.cpp`, and `PythonEnvironment.cpp` to use the new public surface, dropping the private header include from each.

**Tech Stack:** C++20, CMake, git. Workspaces: `protoCore`, `protoPython`, `protoJS`.

**Reference spec:** `/home/gamarino/Documentos/proyectos/protoPython/docs/superpowers/specs/2026-04-25-test-grammar-coverage-design.md` (sections 4 and 9).

**Discovery during planning:** `ProtoContext::fromString(const char* str, int base = 10)` already exists at `protoCore/headers/protoCore.h:645` and delegates to `Integer::fromString` (verified in `protoCore/core/ProtoContext.cpp:451-453`), already auto-promoting to bignum on overflow. The spec's third proposed addition (`fromIntegerString`) is therefore redundant — only `sign` and `integerToString` remain. The plan reflects this.

---

## File-change map

| File | Operation | Purpose |
| :--- | :--- | :--- |
| `/home/gamarino/Documentos/proyectos/protoCore/headers/protoCore.h` | Modify | Add 2 method declarations to `class ProtoObject` |
| `/home/gamarino/Documentos/proyectos/protoCore/install/include/protoCore.h` | Modify (sync) | Mirror the same additions |
| `/home/gamarino/Documentos/proyectos/protoCore/core/ProtoObject.cpp` | Modify | Add 2 method implementations |
| `/home/gamarino/Documentos/proyectos/protoPython/src/library/BuiltinsModule.cpp` | Modify | Drop `<proto_internal.h>`; migrate calls |
| `/home/gamarino/Documentos/proyectos/protoPython/src/library/ExecutionEngine.cpp` | Modify | Drop `<proto_internal.h>`; migrate calls |
| `/home/gamarino/Documentos/proyectos/protoPython/src/library/PythonEnvironment.cpp` | Modify | Drop `<proto_internal.h>`; migrate calls |
| `/home/gamarino/Documentos/proyectos/protoPython/docs/CPYTHON_CONFORMANCE.md` | Modify | Add F0 completion record |

No new files are created.

---

## Task 0: Capture baseline regression metrics

**Files:** None modified — read-only verification.

- [ ] **Step 1: Confirm protoPython is buildable in current state**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython/build && cmake --build . -j$(nproc) 2>&1 | tail -10
```
Expected: build completes; some warnings OK; no errors.

- [ ] **Step 2: Capture baseline of synthetic generators suite**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy tests/test_generators_synthetic.py 2>&1 | tail -5
```
Expected: `PASS=24/24` (per V145 record). Save the exact output to a working note. **If any test that currently passes breaks during F0, this is the regression baseline.**

- [ ] **Step 3: Capture baseline of custom-suite tests**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && for t in tests/test_decorator.py tests/test_abc.py tests/test_contextlib.py tests/test_dataclasses.py; do
  echo "=== $t ==="
  ./build/src/runtime/protopy "$t" 2>&1 | tail -3
done
```
Expected: each test reports OK / all tests pass (per `CPYTHON_CONFORMANCE.md` Necessary section).

- [ ] **Step 4: Capture baseline of test_json**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy test/cpython/test_json.py 2>&1 | tail -5
```
Expected: `OK` with 9/9 PASS.

- [ ] **Step 5: Confirm current `proto_internal.h` consumers**

Run:
```bash
grep -rn "proto_internal.h" /home/gamarino/Documentos/proyectos/protoPython/src/
```
Expected: exactly 3 hits, in `BuiltinsModule.cpp:9`, `ExecutionEngine.cpp:2`, `PythonEnvironment.cpp:7`. If a 4th file appears, this plan needs an extra migration task added before Task 4.

- [ ] **Step 6: Confirm no commit yet — baseline is in working tree**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && git status --short | head -10
```
Expected: only the previously staged `lib/python3.14/abc.py` and untracked build artifacts. (The spec was already committed as `0e67be59`.)

---

## Task 1: Declare `ProtoObject::integerSign` in protoCore.h

**Files:**
- Modify: `/home/gamarino/Documentos/proyectos/protoCore/headers/protoCore.h` (insert near line 134, after `asLong`)
- Modify: `/home/gamarino/Documentos/proyectos/protoCore/install/include/protoCore.h` (mirror)

- [ ] **Step 1: Read the current declarations around line 130-145**

Use the `Read` tool on `/home/gamarino/Documentos/proyectos/protoCore/headers/protoCore.h` with offset=130 limit=20 to confirm the surrounding context before editing.

- [ ] **Step 2: Insert the new declaration in the source header**

In `/home/gamarino/Documentos/proyectos/protoCore/headers/protoCore.h`, locate the line:
```cpp
        long long asLong(ProtoContext* context) const;
```
Insert immediately after it:
```cpp
        /**
         * @brief Bignum-safe sign of an integer object.
         *
         * Returns -1 for negative integers, 0 for zero, +1 for positive.
         * Works for both SmallInteger (tagged) and LargeInteger (heap-allocated)
         * objects. Throws std::runtime_error if the receiver is not an integer.
         *
         * Public replacement for the previously private proto::Integer::sign.
         */
        int integerSign(ProtoContext* context) const;
```

- [ ] **Step 3: Mirror the change in the installed header**

In `/home/gamarino/Documentos/proyectos/protoCore/install/include/protoCore.h`, perform the identical insertion. (This file is the install-side copy that downstream consumers see.)

- [ ] **Step 4: Verify the source header parses (header-only check via touch + build)**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoCore && cmake --build build --target protoCore -j$(nproc) 2>&1 | tail -10
```
Expected: build proceeds. There will be a link-time error if `integerSign` is referenced — but no source uses it yet, so the build should be green.

- [ ] **Step 5: Do not commit yet** — Task 2 adds the matching implementation. Task 3 commits both together so each commit leaves the build green.

---

## Task 2: Implement `ProtoObject::integerSign` in protoCore

**Files:**
- Modify: `/home/gamarino/Documentos/proyectos/protoCore/core/ProtoObject.cpp` (insert after line 850, where `asLong` is implemented)

- [ ] **Step 1: Read the implementation context around line 850**

Use the `Read` tool on `/home/gamarino/Documentos/proyectos/protoCore/core/ProtoObject.cpp` with offset=845 limit=20 to confirm the surrounding context.

- [ ] **Step 2: Insert the implementation**

In `/home/gamarino/Documentos/proyectos/protoCore/core/ProtoObject.cpp`, locate:
```cpp
    long long ProtoObject::asLong(ProtoContext* context) const { return Integer::asLong(context, this); }
```
Insert immediately after:
```cpp
    int ProtoObject::integerSign(ProtoContext* context) const { return Integer::sign(context, this); }
```

- [ ] **Step 3: Build protoCore**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoCore && cmake --build build --target protoCore -j$(nproc) 2>&1 | tail -10
```
Expected: success, no warnings about the new symbol.

- [ ] **Step 4: Run protoCore tests**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoCore && ctest --test-dir build -j$(nproc) --output-on-failure 2>&1 | tail -20
```
Expected: 100% tests passed. (No regression from the addition.)

---

## Task 3: Commit `integerSign` addition

**Files:** All edits from Tasks 1–2.

- [ ] **Step 1: Stage the protoCore changes**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoCore && git status --short
```
Expected: `headers/protoCore.h`, `install/include/protoCore.h`, `core/ProtoObject.cpp` modified.

```bash
cd /home/gamarino/Documentos/proyectos/protoCore && git add headers/protoCore.h install/include/protoCore.h core/ProtoObject.cpp
```

- [ ] **Step 2: Create the commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore && git commit -m "$(cat <<'EOF'
F0-1: expose ProtoObject::integerSign in public API

Adds a public bignum-safe sign accessor on ProtoObject as a
delegator to the internal Integer::sign. This lets downstream
projects (protoPython, protoJS) read integer signs without
including <proto_internal.h>.

No behavior change; new symbol only.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Expected: commit created.

---

## Task 4: Declare and implement `ProtoObject::asIntegerString`

**Files:**
- Modify: `/home/gamarino/Documentos/proyectos/protoCore/headers/protoCore.h`
- Modify: `/home/gamarino/Documentos/proyectos/protoCore/install/include/protoCore.h`
- Modify: `/home/gamarino/Documentos/proyectos/protoCore/core/ProtoObject.cpp`

- [ ] **Step 1: Add the declaration in the source header**

In `/home/gamarino/Documentos/proyectos/protoCore/headers/protoCore.h`, immediately after the `integerSign` declaration just inserted, add:
```cpp
        /**
         * @brief Bignum-safe integer-to-string conversion.
         *
         * Returns a ProtoString containing the receiver's integer value
         * rendered in the given base (2..36). Works for SmallInteger and
         * LargeInteger objects. Throws std::invalid_argument for an
         * out-of-range base, std::runtime_error if the receiver is not
         * an integer.
         *
         * Public replacement for the previously private
         * proto::Integer::toString.
         */
        const ProtoString* asIntegerString(ProtoContext* context, int base = 10) const;
```

- [ ] **Step 2: Mirror in the installed header**

Apply the identical insertion in `/home/gamarino/Documentos/proyectos/protoCore/install/include/protoCore.h`.

- [ ] **Step 3: Add the implementation**

In `/home/gamarino/Documentos/proyectos/protoCore/core/ProtoObject.cpp`, immediately after the `integerSign` implementation just inserted, add:
```cpp
    const ProtoString* ProtoObject::asIntegerString(ProtoContext* context, int base) const { return Integer::toString(context, this, base); }
```

- [ ] **Step 4: Build protoCore**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoCore && cmake --build build --target protoCore -j$(nproc) 2>&1 | tail -10
```
Expected: success.

- [ ] **Step 5: Run protoCore tests**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoCore && ctest --test-dir build -j$(nproc) --output-on-failure 2>&1 | tail -10
```
Expected: 100% pass.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoCore && git add headers/protoCore.h install/include/protoCore.h core/ProtoObject.cpp && git commit -m "$(cat <<'EOF'
F0-2: expose ProtoObject::asIntegerString in public API

Adds a public bignum-safe integer-to-string accessor on
ProtoObject as a delegator to the internal Integer::toString.
Lets downstream projects render arbitrary-precision integers
without including <proto_internal.h>.

No behavior change; new symbol only.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: ABI safety gate — rebuild protoJS

**Files:** None modified — verification only.

- [ ] **Step 1: Rebuild protoJS against the updated protoCore**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoJS && cmake --build build -j$(nproc) 2>&1 | tail -15
```
Expected: success. No undefined-symbol errors.

- [ ] **Step 2: If a `protojs` binary builds, smoke it**

Run (only if the binary exists):
```bash
test -x /home/gamarino/Documentos/proyectos/protoJS/build/protojs && /home/gamarino/Documentos/proyectos/protoJS/build/protojs -e "console.log('R1 OK:', 1 + 1)"
```
Expected: `R1 OK: 2`. (R1 is the ABI risk from spec section 8.)

If this step fails, halt the plan, do not advance to Task 6, and investigate. Likely cause: ABI symbol mismatch from the protoCore install layout.

---

## Task 6: Migrate `BuiltinsModule.cpp` off `proto_internal.h`

**Files:**
- Modify: `/home/gamarino/Documentos/proyectos/protoPython/src/library/BuiltinsModule.cpp`

- [ ] **Step 1: Inventory the file's uses of private symbols**

Run:
```bash
grep -nE "proto::Integer::|proto_internal" /home/gamarino/Documentos/proyectos/protoPython/src/library/BuiltinsModule.cpp
```
Record the line numbers and the specific calls. Expected hits:
- `BuiltinsModule.cpp:9` — `#include <proto_internal.h>`
- `BuiltinsModule.cpp:602` — `proto::Integer::sign(context, obj)`
- `BuiltinsModule.cpp:729` — `proto::Integer::toString(context, obj, 10)`

(Verify with the grep output. If hits differ, follow what's in the file.)

- [ ] **Step 2: Replace each `proto::Integer::sign` call**

For each occurrence found in step 1, replace
```cpp
proto::Integer::sign(context, obj)
```
with
```cpp
obj->integerSign(context)
```
The `context` and `obj` variable names may differ between call sites — keep the actual local-variable names. The semantics are identical.

- [ ] **Step 3: Replace each `proto::Integer::toString` call**

For each occurrence found in step 1, replace
```cpp
proto::Integer::toString(context, obj, 10)
```
with
```cpp
obj->asIntegerString(context, 10)
```
(Or with whatever base argument was being used.)

- [ ] **Step 4: Remove the `#include <proto_internal.h>` line**

Edit `BuiltinsModule.cpp` line 9. Replace:
```cpp
#include <proto_internal.h>
```
with the line removed entirely. Confirm `<protoCore.h>` is still included (it should already be).

- [ ] **Step 5: Build protoPython**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython/build && cmake --build . --target protopy -j$(nproc) 2>&1 | tail -15
```
Expected: success. If the build fails with `undefined reference to proto::Integer::*`, a call site was missed in step 2 or 3 — go back and check.

- [ ] **Step 6: Smoke run on a simple Python expression that uses `int.__sign__` paths**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy -c "print(abs(-7), abs(2**80), str(2**100))"
```
Expected: `7 1208925819614629174706176 1267650600228229401496703205376`. **Critical: the third number proves bignum→string still works after the migration.**

- [ ] **Step 7: Run synthetic generators suite (Task 0 baseline)**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy tests/test_generators_synthetic.py 2>&1 | tail -3
```
Expected: 24/24 PASS — same as Task 0 baseline. **If any test regresses, revert this task and investigate.**

- [ ] **Step 8: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoPython && git add src/library/BuiltinsModule.cpp && git commit -m "$(cat <<'EOF'
F0-3: BuiltinsModule.cpp uses public protoCore API only

Replaces proto::Integer::sign and proto::Integer::toString
with the new public ProtoObject::integerSign and
ProtoObject::asIntegerString accessors. Drops the
#include <proto_internal.h>.

No behavior change. Bignum->string verified end-to-end:
print(str(2**100)) returns the correct 31-digit decimal.

Synthetic suite: 24/24 (no regression).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Migrate `ExecutionEngine.cpp` off `proto_internal.h`

**Files:**
- Modify: `/home/gamarino/Documentos/proyectos/protoPython/src/library/ExecutionEngine.cpp`

- [ ] **Step 1: Inventory the file's uses**

Run:
```bash
grep -nE "proto::Integer::|proto_internal" /home/gamarino/Documentos/proyectos/protoPython/src/library/ExecutionEngine.cpp
```
Expected hits include:
- `ExecutionEngine.cpp:2` — `#include <proto_internal.h>`
- `ExecutionEngine.cpp:1287` — `proto::Integer::sign(ctx, o)`
- `ExecutionEngine.cpp:1304` — `proto::Integer::toString(ctx, o, 10)`
- `ExecutionEngine.cpp:1331` — `proto::Integer::sign(ctx, o)`
- `ExecutionEngine.cpp:1348` — `proto::Integer::toString(ctx, o, 10)`

(If grep shows different line numbers, follow the file.)

- [ ] **Step 2: Replace each call (same pattern as Task 6, steps 2–3)**

For each `proto::Integer::sign(ctx, o)`, change to `o->integerSign(ctx)`.
For each `proto::Integer::toString(ctx, o, 10)`, change to `o->asIntegerString(ctx, 10)`.

Variable names may differ (`ctx` vs `context`, `o` vs `obj`); preserve the local names.

- [ ] **Step 3: Remove the `#include <proto_internal.h>`**

Edit `ExecutionEngine.cpp` line 2 to delete the include line.

- [ ] **Step 4: Build protoPython**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython/build && cmake --build . --target protopy -j$(nproc) 2>&1 | tail -15
```
Expected: success.

- [ ] **Step 5: Smoke regression — generators synthetic + a runtime arithmetic check**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy tests/test_generators_synthetic.py 2>&1 | tail -3
```
Expected: 24/24 PASS.

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy -c "x = -2**70; print(x // 3, x % 3)"
```
Expected: `-393450392437588152555 1` (large divmod path uses sign internally — verifies the migrated code works).

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoPython && git add src/library/ExecutionEngine.cpp && git commit -m "$(cat <<'EOF'
F0-4: ExecutionEngine.cpp uses public protoCore API only

Replaces proto::Integer::sign and proto::Integer::toString
with ProtoObject::integerSign and ProtoObject::asIntegerString.
Drops #include <proto_internal.h>.

Synthetic suite: 24/24 (no regression).
Bignum divmod with negative dividend verified.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Migrate `PythonEnvironment.cpp` off `proto_internal.h`

**Files:**
- Modify: `/home/gamarino/Documentos/proyectos/protoPython/src/library/PythonEnvironment.cpp`

- [ ] **Step 1: Inventory the file's uses**

Run:
```bash
grep -nE "proto::Integer::|proto_internal" /home/gamarino/Documentos/proyectos/protoPython/src/library/PythonEnvironment.cpp
```
Expected hits:
- `PythonEnvironment.cpp:7` — `#include <proto_internal.h>`
- `PythonEnvironment.cpp:855` — `proto::Integer::toString(ctx, val, 10)`
- `PythonEnvironment.cpp:1110` — `proto::Integer::sign(ctx, obj)`
- `PythonEnvironment.cpp:1169` — `proto::Integer::toString(context, self, 10)`

(Confirm line numbers by running the grep.)

The file also has a comment block at lines 3-6 referencing `proto_internal.h`'s purpose — update that comment to reflect the new state.

- [ ] **Step 2: Replace each `proto::Integer::sign` call**

Apply the same substitution as Task 6 step 2.

- [ ] **Step 3: Replace each `proto::Integer::toString` call**

Apply the same substitution as Task 6 step 3.

- [ ] **Step 4: Update the leading comment**

In `PythonEnvironment.cpp` lines 3-6 (originally explaining why `<proto_internal.h>` is needed for bignum), the explanation is now obsolete. Read the existing comment first, then either delete it (if it adds no remaining value) or rewrite to reflect that bignum operations now use the public `ProtoObject::integerSign` and `ProtoObject::asIntegerString` accessors.

- [ ] **Step 5: Remove the `#include <proto_internal.h>`**

Edit `PythonEnvironment.cpp` line 7 to delete the include line.

- [ ] **Step 6: Build protoPython**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython/build && cmake --build . --target protopy -j$(nproc) 2>&1 | tail -15
```
Expected: success.

- [ ] **Step 7: Confirm zero `proto_internal.h` consumers remain**

Run:
```bash
grep -rn "proto_internal.h" /home/gamarino/Documentos/proyectos/protoPython/src/
```
Expected: empty output. If any line is reported, a fourth file was missed — open it and migrate using the same pattern; do not commit Task 8 until clean.

- [ ] **Step 8: Smoke regression — synthetic + a `repr` check on a bignum**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy tests/test_generators_synthetic.py 2>&1 | tail -3
```
Expected: 24/24 PASS.

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy -c "n = 2**200; print(repr(n), len(str(n)))"
```
Expected: a 61-digit number followed by ` 61` (verifies `str(bignum)` and `repr(bignum)` both go through the migrated `asIntegerString` path).

- [ ] **Step 9: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoPython && git add src/library/PythonEnvironment.cpp && git commit -m "$(cat <<'EOF'
F0-5: PythonEnvironment.cpp uses public protoCore API only

Replaces proto::Integer::sign and proto::Integer::toString
with ProtoObject::integerSign and ProtoObject::asIntegerString.
Drops #include <proto_internal.h> and updates the leading
comment block.

protoPython source no longer includes any private protoCore
header.

Synthetic suite: 24/24 (no regression).
str(2**200) and repr(2**200) verified end-to-end.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Full smoke regression suite

**Files:** None modified — verification only.

- [ ] **Step 1: Run the four custom-suite tests**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && for t in tests/test_decorator.py tests/test_abc.py tests/test_contextlib.py tests/test_dataclasses.py; do
  echo "=== $t ==="
  ./build/src/runtime/protopy "$t" 2>&1 | tail -3
done
```
Expected: each test reports OK / all pass — same as Task 0 baseline.

- [ ] **Step 2: Run test_json**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy test/cpython/test_json.py 2>&1 | tail -5
```
Expected: 9/9 PASS.

- [ ] **Step 3: Run test_grammar (it will still crash in compilation, but record the state)**

Run:
```bash
cd /home/gamarino/Documentos/proyectos/protoPython && ./build/src/runtime/protopy lib/python3.14/test/test_grammar.py 2>&1 | tail -10
```
Expected: same crash signature as Task 0 (BinOpNode at line 1615). F0 does not fix this; F1 will. Recording it here proves F0 did not introduce a *new* failure mode.

- [ ] **Step 4: If any custom-suite test regresses, halt and revert**

If any of the suites that passed at Task 0 now fails:
1. Stop the plan.
2. Identify which task introduced the regression (`git bisect F0-1..HEAD`).
3. Revert that commit and re-plan.

If all suites pass: proceed to Task 10.

---

## Task 10: Update `CPYTHON_CONFORMANCE.md` with F0 record

**Files:**
- Modify: `/home/gamarino/Documentos/proyectos/protoPython/docs/CPYTHON_CONFORMANCE.md`

- [ ] **Step 1: Read the current top of the file (where the V<n> entries live)**

Use the `Read` tool with `offset=85` and `limit=10` on `docs/CPYTHON_CONFORMANCE.md` to find the most recent `### V<n> Changes` heading.

- [ ] **Step 2: Insert a new `### V154 Changes` section after the existing top-of-changelog**

Locate the line `### V153 Changes (2026-04-25) — PI: close all metaclass + descriptor tests (37/0/0)` (or whatever the most recent version number is). Insert immediately *before* it:

```markdown
### V154 Changes (2026-04-25) — F0: bignum API made public; proto_internal.h dropped from protoPython

The F0 round of the test_grammar.py 75/75 coverage push (see
`docs/superpowers/specs/2026-04-25-test-grammar-coverage-design.md`)
extends `protoCore.h`'s public API and removes every
`#include <proto_internal.h>` from protoPython source.

**F0-1, F0-2 — Public bignum accessors on ProtoObject**
Adds `ProtoObject::integerSign(ctx)` and
`ProtoObject::asIntegerString(ctx, base)` to
`protoCore/headers/protoCore.h`, implemented in
`protoCore/core/ProtoObject.cpp` as one-line delegators to the
existing internal `Integer::sign` / `Integer::toString`.
`ProtoContext::fromString(str, base)` already auto-promotes to
bignum via `Integer::fromString` (verified at
`protoCore/core/ProtoContext.cpp:451-453`); no new factory
needed.

**F0-3, F0-4, F0-5 — Migrate the 3 protoPython consumers**
`BuiltinsModule.cpp`, `ExecutionEngine.cpp`, and
`PythonEnvironment.cpp` now call the public accessors
exclusively. `#include <proto_internal.h>` removed from each.

**Verification**
- protoCore tests: 100% pass (no ABI break).
- protoJS rebuild: green (R1 mitigated).
- Synthetic generators suite: 24/24 (no regression).
- test_json: 9/9 (no regression).
- Custom suites (test_decorator, test_abc, test_contextlib,
  test_dataclasses): all pass.
- test_grammar.py: still crashes in compilation as in V136
  (the BinOpNode bug is the F1 target, not F0).
- `grep -rn "proto_internal.h" protoPython/src/` returns empty.
```

- [ ] **Step 3: Commit the documentation update**

```bash
cd /home/gamarino/Documentos/proyectos/protoPython && git add docs/CPYTHON_CONFORMANCE.md && git commit -m "$(cat <<'EOF'
F0-6: record V154 (F0 round) in CPYTHON_CONFORMANCE.md

Documents the F0 round: public bignum API extension and
proto_internal.h removal from protoPython source. All
regression suites pass. test_grammar.py is unchanged
(its compilation bug is the F1 target).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Acceptance criteria for F0

The phase is complete when **all** of these hold:

| Check | Command | Expected |
| :--- | :--- | :--- |
| protoPython source has no private-header includes | `grep -rn "proto_internal.h" protoPython/src/` | empty |
| protoCore builds and tests pass | `cd protoCore && cmake --build build && ctest --test-dir build` | 100% pass |
| protoJS builds against the new protoCore | `cd protoJS && cmake --build build` | success |
| protoPython builds | `cd protoPython/build && cmake --build .` | success |
| Synthetic generators suite | `./build/src/runtime/protopy tests/test_generators_synthetic.py` | 24/24 |
| test_json | `./build/src/runtime/protopy test/cpython/test_json.py` | 9/9 |
| Custom suites | `./build/src/runtime/protopy tests/test_{decorator,abc,contextlib,dataclasses}.py` | all OK |
| test_grammar baseline preserved (still crashes at the same point) | `./build/src/runtime/protopy lib/python3.14/test/test_grammar.py` | same crash as Task 0 |
| Commit history | `git log --oneline F0-baseline..HEAD` | exactly 6 commits: F0-1 through F0-6 |
| CHANGELOG | grep `V154` in `docs/CPYTHON_CONFORMANCE.md` | present |

When all rows are green, F0 is closed. The next plan to write is `2026-04-25-test-grammar-F1.md` (compilation unblock).

---

## Notes for the executing engineer

- **No hacks, no stubs.** If the migration of any call site is unclear because a wrapper has slightly different semantics, halt and report rather than guessing. The internal `Integer::*` methods throw on type mismatch; the public `ProtoObject::*` methods inherit that behavior — no exception-handling change should be needed.
- **Variable naming.** The `proto::Integer::sign(ctx, o)` calls use `ctx` or `context` and `o` or `obj` interchangeably across the 3 source files. Keep the existing local names; do not rename for cosmetic consistency.
- **Build flavor.** All commands in this plan target `build/` (the default cmake build dir). If the user is working in `build-debug/`, `build-release/`, or `build-lto/`, substitute that path everywhere. The presence of multiple build dirs in the repo (`build/`, `build-debug/`, `build-release/`, `build-lto/`, `build-profile/`, `cmake-build-debug/`, etc.) means the engineer should pick one canonical build for verification.
- **Bignum proof points.** The smoke commands `print(str(2**100))`, `print(str(2**200))`, and `print(-2**70 // 3)` are not redundant: they exercise different bignum code paths (encode, decode, sign+toString together). Keep all three.
- **R8 (regression in other suites).** The smoke regression in Tasks 6, 7, 8, 9 only runs the 5 fast suites. If a regression appears in a slower suite (e.g., `test_types`), it will be caught in the F1 plan's broader baseline. The F0 contract is "no regression in fast suites + no new mode of failure".
- **If a `proto_internal.h` inclusion turns up in a header.** None do today (verified by the grep in Task 0 step 5), but if Task 8 step 7 finds one in `src/library/*.h` or `include/protoPython/`, that header must be migrated by the same pattern — public delegators only. Do not introduce a `using proto::Integer;` shortcut.
