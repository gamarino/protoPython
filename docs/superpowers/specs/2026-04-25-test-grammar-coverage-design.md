# Test Grammar Coverage — Design Specification

**Date:** 2026-04-25
**Owner:** protoPython core
**Goal:** Achieve full coverage of CPython's `test_grammar.py` (75/75 PASS, 5 skip honoured) under protoPython, while removing all dependencies on `proto_internal.h` from protoPython source code.

---

## 1. Background and motivation

`test_grammar.py` (`lib/python3.14/test/test_grammar.py`, 2112 lines, 75 test functions) is the canonical Python language conformance suite. Achieving full pass rate is the primary criterion for protoPython's "All Green Essential" milestone documented in `docs/CPYTHON_CONFORMANCE.md`.

Current state at the start of this work (V136 baseline, 2026-04-24):

- 33/75 PASS, 5 skip, 49 fail + 19 err.
- Most recent invocation reports a **compile-time crash** in `Compiler::compileNode` (BinOpNode at line 1615 of the test file), causing the suite to fail to load entirely (effective state: **0/75 PASS, 1 CRASH**).
- 3 source files in `src/library/` (`BuiltinsModule.cpp`, `ExecutionEngine.cpp`, `PythonEnvironment.cpp`) include `proto_internal.h`, which is a private header reserved for protoCore-internal debugging.

User-imposed constraints:

- **Full implementations only.** No mocks, no stubs, no hacks.
- **Public API only.** protoPython source code must not include `proto_internal.h`. Only `protoCore.h` is permitted.

---

## 2. Decisions captured

| Question | Decision |
| :--- | :--- |
| `proto_internal.h` handling | Eliminate every existing include of `proto_internal.h` from protoPython source. |
| Public API gap | Extend `protoCore.h` with the missing surface (`fromIntegerString`, `proto::sign`, `proto::integerToString`). protoCore is part of the workspace and may be modified. |
| Coverage target | 75/75 PASS, 5 skip honoured, 0 CRASH. |
| Commit granularity | One fix per commit (granularity A). Each commit is independently bisectable and ships a stats delta. |
| Verification | Each commit must rebuild green, pass the synthetic test (when applicable), pass `test_grammar.py` without regression, and not regress the four custom-suite tests (`test_decorator`, `test_abc`, `test_contextlib`, `test_dataclasses`) or `test_json`. |

---

## 3. Architecture: phases F0 through F13

The work proceeds as a sequence of phases. Each phase is composed of multiple commits (granularity A). Phases are mostly sequential — F0 must finish before F1, but later phases may overlap if a fix naturally unblocks tests in a different cluster.

| Phase | Name | Output | Commit budget |
| :---: | :--- | :--- | :---: |
| **F0** | Bignum API + `proto_internal.h` removal | `protoCore.h` extended with public bignum surface; 3 protoPython files migrated; protoJS rebuild verified | 5–8 |
| **F1** | Compilation unblock | `test_grammar.py` loads; baseline X/75 measured cleanly | 1–3 |
| **F2** | Numeric literals & bignum | 8 tests green | 5–10 |
| **F3** | String / bytes literals | 4 tests green | 3–6 |
| **F4** | Operators | 7 tests green | 4–8 |
| **F5** | Atoms / selectors / paren-eval | 4 tests green (large tests) | 6–12 |
| **F6** | Comprehensions | 4 tests green | 4–8 |
| **F7** | Compound stmts (if/while/for/try) | 5 tests green | 4–8 |
| **F8** | Loop / return / yield control | 7 tests green | 4–8 |
| **F9** | Funcdef + lambdef | 3 tests green (one is 248 lines) | 8–15 |
| **F10** | Annotations (PEP 526/563/649) | 8 tests green | 6–12 |
| **F11** | Class / with / matrix mul | 4 tests green | 4–8 |
| **F12** | Assert family | 4 tests green | 3–6 |
| **F13** | Async / import / scope / misc | 12 tests green | 8–15 |

Total estimated commit budget: **65–110 commits**.

---

## 4. Phase F0 — Bignum API and `proto_internal.h` removal

### 4.1 protoCore public API extensions

Three additions to `protoCore/headers/protoCore.h`. **No existing symbol changes.**

```cpp
namespace proto {

    // Auto-promoting integer parser. Accepts a UTF-8 C string and a base
    // (2..36, or 0 for prefix-based detection: 0x, 0o, 0b). Returns either
    // a tagged SmallInteger or a heap-allocated LargeInteger as a
    // ProtoObject*. Returns nullptr on parse error.
    class ProtoContext {
        ...
        const ProtoObject* fromIntegerString(const char* str, int base = 10);
    };

    // Bignum-safe sign extraction. Returns -1, 0, or +1 for any integer
    // object (SmallInteger or LargeInteger). UB if obj is not an integer.
    int sign(ProtoContext* context, const ProtoObject* obj);

    // Bignum-safe integer-to-string. Handles arbitrary-width integers in
    // bases 2..36. Returns nullptr if obj is not an integer.
    const ProtoString* integerToString(ProtoContext* context,
                                       const ProtoObject* obj,
                                       int base = 10);

}  // namespace proto
```

These delegate internally to the existing `Integer::*` static methods. Implementation lives in `protoCore/core/Integer.cpp` (or wherever the class is defined today).

### 4.2 Migration of the 3 source files

For each of `BuiltinsModule.cpp`, `ExecutionEngine.cpp`, `PythonEnvironment.cpp`:

1. Remove `#include <proto_internal.h>`.
2. Replace `proto::Integer::sign(ctx, o)` with `proto::sign(ctx, o)`.
3. Replace `proto::Integer::toString(ctx, o, base)` with `proto::integerToString(ctx, o, base)`.
4. Replace `proto::Integer::fromString(...)` with `ctx->fromIntegerString(...)`.
5. Audit any other private symbols accidentally consumed (forward-declared classes, internal `Cell` subclasses). If found, document and design a public wrapper before proceeding.

### 4.3 F0 verification gate

- `cd protoCore && cmake --build build` — green.
- `cd protoPython && cmake --build build` — green.
- `cd protoJS && cmake --build build` — green (R1 mitigation).
- `grep -rn "proto_internal.h" protoPython/src/` — empty.
- `./build/src/runtime/protopy tests/test_generators_synthetic.py` — 24/24.
- All four custom-suite tests still pass.

Only when every box is checked may F1 begin.

---

## 5. Phase F1 — Compilation unblock

The most recent test run (`tests/test_grammar_result.txt`) shows `test_grammar.py` aborts before any test runs:

```
Compiler::compileNode FAILED for node type N11protoPython9BinOpNodeE at line 1615
Compiler::compileNode FAILED for node type N11protoPython10AssignNodeE at line 1614
Compiler::compileNode FAILED for node type N11protoPython9SuiteNodeE at line 1614
Compiler::compileNode FAILED for node type N11protoPython15FunctionDefNodeE at line 1613
Compiler::compileNode FAILED for node type N11protoPython9SuiteNodeE at line 269
Compiler::compileNode FAILED for node type N11protoPython12ClassDefNodeE at line 267
protopy: compilation error in 'lib/python3.14/test/test_grammar.py'
```

F1 diagnoses the BinOpNode compile failure, identifies the missing handler in `Compiler.cpp`, and ships the fix. After F1 the suite *runs* and a clean baseline can be measured.

### 5.1 F1 verification gate

- `./build/src/runtime/protopy lib/python3.14/test/test_grammar.py 2>&1 | tail -5` reports `Ran 75 tests in ...` with **0 CRASH**.
- The number of tests run equals 75 (none aborted by import failure).
- A new line in `docs/CPYTHON_CONFORMANCE.md` records the new clean baseline.

The number of FAILs may *increase* relative to V136's 49 — that is expected. F1's success is measured by **CRASH = 0**, not by FAIL = monotonic.

---

## 6. Cluster taxonomy (F2–F13)

The 75 test functions, grouped by likely shared root cause. Phases attack clusters in the order shown.

| Phase | Cluster | Tests in cluster |
| :---: | :--- | :--- |
| F2 | Numeric & bignum | `test_backslash`, `test_plain_integers`, `test_long_integers`, `test_floats`, `test_float_exponent_tokenization`, `test_underscore_literals`, `test_bad_numerical_literals`, `test_end_of_numerical_literals` |
| F3 | String / bytes | `test_string_literals`, `test_string_prefixes`, `test_bytes_prefixes`, `test_ellipsis` |
| F4 | Operators | `test_binary_mask_ops`, `test_shift_ops`, `test_additive_ops`, `test_multiplicative_ops`, `test_unary_ops`, `test_comparison`, `test_comparison_is_literal` |
| F5 | Atoms / selectors / paren | `test_atoms`, `test_selectors`, `test_warn_missed_comma`, `test_paren_evaluation` |
| F6 | Comprehensions | `test_dictcomps`, `test_listcomps`, `test_genexps`, `test_comprehension_specials` |
| F7 | Compound stmts | `test_if`, `test_while`, `test_for`, `test_try`, `test_try_star` |
| F8 | Loop / return / yield | `test_break_stmt`, `test_continue_stmt`, `test_break_continue_loop`, `test_return`, `test_yield`, `test_yield_in_comprehensions`, `test_control_flow_in_finally` |
| F9 | Funcdef + lambdef | `test_funcdef` (248 lines), `test_lambdef`, `test_complex_lambda` |
| F10 | Annotations | `test_var_annot_basics`, `test_var_annot_syntax_errors`, `test_var_annot_basic_semantics`, `test_annotations_inheritance`, `test_var_annot_module_semantics`, `test_var_annot_in_module`, `test_var_annot_simple_exec`, `test_var_annot_rhs` |
| F11 | Class / with / matrix | `test_classdef`, `test_with_statement`, `test_matrix_mul`, `test_if_else_expr` |
| F12 | Assert | `test_assert`, `test_assert_failures`, `test_assert_syntax_warnings`, `test_assert_warning_promotes_to_syntax_error` |
| F13 | Async / import / scope / misc | `test_async_await`, `test_async_for`, `test_async_with`, `test_import`, `test_global`, `test_nonlocal`, `test_raise`, `test_simple_stmt`, `test_expr_stmt`, `test_del_stmt`, `test_pass_stmt`, `test_former_statements_refer_to_builtins`, `test_eof_error`, `test_max_level`, `test_eval_input`, `test_test`, `test_suite` |

Cluster ordering rationale:

- **F2 first** so that bignum API (F0) is exercised end-to-end by real workloads before phases F3+.
- **F3, F4** are tokenizer / expression-level: low blast radius.
- **F5, F6** are the largest of the interpreter; tackled when F2–F4 are green so debugging is unambiguous.
- **F7–F11** depend on each other (closures used by funcdef, classdef, with, comprehensions); ordered to keep the dependency DAG topological.
- **F12–F13** are deferred: assert is mostly polish; async carries known coroutine-resumption issues already documented in V148/V149.

---

## 7. Per-commit verification protocol

```text
For each fix:
  1. Reproduce the failure with a minimal synthetic case in
     tests/test_grammar_F<n>_synthetic.py
  2. Incremental rebuild:
       cd build && cmake --build . -j$(nproc)
  3. Run synthetic:
       ./src/runtime/protopy ../tests/test_grammar_F<n>_synthetic.py
  4. Run full grammar suite:
       ./src/runtime/protopy ../lib/python3.14/test/test_grammar.py
       2>&1 | tail -5
  5. If delta(PASS) >= 0 AND delta(CRASH) <= 0:
       update docs/CPYTHON_CONFORMANCE.md (stats line)
       update tasks/lessons.md (only if a new pattern is exposed)
       commit
     Else:
       rollback the change and rethink the fix
  6. Smoke regression run:
       test_decorator, test_abc, test_contextlib, test_dataclasses, test_json
     If any regress: revert the commit before pushing
```

### 7.1 Commit message format

```
F<n>-<step>: <short title of the fix>

Cause: <2–3 lines on what was wrong in the interpreter>
Fix: <2–3 lines on the solution, citing file:line>
Stats: test_grammar.py X/75 -> Y/75 (+delta)
Synthetic: tests/test_grammar_F<n>_synthetic.py — A/B -> C/D
```

### 7.2 Versioned per commit

- Source files in `src/library/`, `src/compiler/`, or `protoCore/`
- The synthetic test file (when applicable)
- `docs/CPYTHON_CONFORMANCE.md` stats line
- `tasks/lessons.md` (only when a new pattern is identified)

### 7.3 Excluded from commits

- Build artifacts (`build*/`, `*.o`, `*.so`)
- Temporary debug logs (`debug_log.txt`, `diag*.log`, `repro_*.log`)

---

## 8. Risk register

| # | Risk | Probability | Impact | Mitigation |
| :---: | :--- | :--- | :--- | :--- |
| R1 | F0 breaks ABI; protoJS or others fail to compile | Medium | High | Only add new symbols. protoJS rebuild required as part of F0 gate. |
| R2 | Public bignum wrappers add overhead | Low | Medium | Wrappers are inline / thin. Run `benchmarks/` before and after F0. |
| R3 | F1 fix exposes many new FAILs | High | Medium | F1's success criterion is CRASH = 0, not FAIL = monotonic. |
| R4 | A single cluster takes more than 15 commits | High | Low | Allowed by design; bifurcate into sub-phases (e.g., F9a, F9b) if needed. |
| R5 | F10 requires PEP 649 deferred annotations not implemented | Medium | High | Halt F10 and renegotiate scope; either implement PEP 649 or downgrade specific tests to "honest coverage". |
| R6 | PEP 695 type params or PEP 750 t-strings appear | Medium | Medium | Halt and renegotiate before proceeding. |
| R7 | Async tests still hit coroutine resumption bug from V148 | Medium | Medium | F13 is deliberately last. May spawn a dedicated `F13-async-round`. |
| R8 | A fix regresses other suites (test_types, test_json, test_decorator, …) | High | High | Smoke regression run on every commit. Immediate revert on any regression. |
| R9 | The branch becomes hard to review at >100 commits | Low | Medium | Each cluster closes with a summary commit linking the cluster's fix commits and reporting cumulative stats. |
| R10 | The user interrupts mid-phase | Medium | Low | Each commit leaves the repo green. Any prefix of the commit list is a consistent state. |

### 8.1 Mandatory renegotiation gates

- After F0, before F1: confirm the public API extension as merged.
- If R5 or R6 materializes: halt; write a note in `tasks/todo.md`; await user decision.
- If a phase exceeds 1.5x its commit budget without unblocking the last 20% of its tests: halt; report; ask whether to continue, escalate, or reduce scope.

---

## 9. File-change map

### 9.1 protoCore (only F0)

| File | Change |
| :--- | :--- |
| `protoCore/headers/protoCore.h` | Add public declarations: `ProtoContext::fromIntegerString`, `proto::sign`, `proto::integerToString`. No edits to existing symbols. |
| `protoCore/core/Integer.cpp` (or equivalent) | Implement the new public wrappers, delegating to existing internal logic. |
| `protoCore/install/include/protoCore.h` | Sync with the source header. |

### 9.2 protoPython library (F0–F13, by phase)

| File | Phases | Nature of change |
| :--- | :--- | :--- |
| `src/library/BuiltinsModule.cpp` | F0, F2, F4, F12 | F0: drop `<proto_internal.h>`, migrate. F2/F4: numeric builtins (`int()`, `float()`, `abs()`, `divmod()`). F12: assert support. |
| `src/library/ExecutionEngine.cpp` | F0, F4, F7, F8, F11, F13 | F0: migrate. F4: arithmetic opcodes. F7: control-flow opcodes. F8: yield / return. F11: with / match. F13: async machinery. |
| `src/library/PythonEnvironment.cpp` | F0, F2, F10 | F0: migrate. F2: bignum-aware coercion. F10: annotation evaluation. |
| `src/library/Compiler.cpp` | F1, F5, F6, F9, F10, F11 | F1: the BinOpNode compile bug. F5: selectors / atoms parsing. F6: comprehensions. F9: funcdef. F10: annotations. F11: classdef / with. |
| `src/library/ExceptionsModule.cpp` | F12 | Assert and warning machinery. |

### 9.3 protoPython tests

```
tests/test_grammar_F2_synthetic.py
tests/test_grammar_F3_synthetic.py
...
tests/test_grammar_F13_synthetic.py
```

Each file follows the pattern of the existing `tests/test_generators_synthetic.py`: a list of independent cases each in a `try/except`, with PASS/FAIL/CRASH reported per case, exit 0 on full pass.

### 9.4 Documentation updates

| File | Cadence |
| :--- | :--- |
| `docs/CPYTHON_CONFORMANCE.md` | Stats line updated on every commit. New `### V<n> Changes` section at the close of each phase. |
| `tasks/lessons.md` | Only when a fix exposes a new general pattern. |
| `tasks/todo.md` | Phase boundary updates (mark Fn complete). |
| `CHANGELOG.md` | One entry per phase (not per commit). |

### 9.5 Not versioned

- `tests/test_grammar_result.txt` (overwritten between runs)
- `docs/superpowers/specs/2026-04-25-test-grammar-coverage-design.md` (this file — versioned, but written once at design time)

---

## 10. Acceptance criteria

The work is complete when **all** of the following hold:

| Metric | Target |
| :--- | :--- |
| `test_grammar.py` PASS | 75 / 75 |
| `test_grammar.py` SKIP | 5 (CPython `@unittest.skip` decorators honoured) |
| `test_grammar.py` CRASH | 0 |
| `proto_internal.h` includes in protoPython source | 0 |
| Regression in `test_json`, `test_decorator`, `test_abc`, `test_contextlib`, `test_dataclasses` | 0 |
| `protoJS` build after F0 | green |
| `docs/CPYTHON_CONFORMANCE.md` `test_grammar.py` line | reads `**PASS** — 75/75 tests pass` |
| `git log` from baseline to HEAD | bisectable; one improvement per commit |

### 10.1 Definition of done — single-shot verification

```bash
cd /home/gamarino/Documentos/proyectos/protoPython/build
./src/runtime/protopy ../lib/python3.14/test/test_grammar.py 2>&1 | tail -3
# Expected:
#   ----------------------------------------------------------------------
#   Ran 75 tests in <T>s
#   OK (skipped=5)

grep -rn "proto_internal.h" /home/gamarino/Documentos/proyectos/protoPython/src/
# Expected: empty.
```

---

## 11. Out of scope

The following are intentionally not part of this work:

- Any fix that requires changing protoPython's public CLI or DSL.
- Performance optimization. Bignum wrappers are added with delegate-style implementations; benchmark drift is monitored (R2) but not fixed under this spec.
- Migration of any *other* `proto_internal.h` consumer outside the 3 named files. (No others were found in `src/library/`; if any appear in non-library directories, treat as a separate scope item.)
- New language features beyond what `test_grammar.py` exercises.
- `test_types.py`, `test_descr.py`, or any other CPython conformance file. These follow after this work in their own design cycles.

---

## 12. Approval

User decisions captured in conversation 2026-04-25:

- proto_internal.h handling: **B** (eliminate all uses)
- Public API extension: **A** (extend protoCore.h)
- Coverage target: **A** (75/75 PASS, 5 skip honoured)
- Commit granularity: **A** (one fix per commit)
- Implementation approach: **2** (cluster by feature)
- Sections 1–6 of the design: **OK** (each approved in turn)

The next step is to invoke `superpowers:writing-plans` to produce the detailed step-by-step implementation plan.
