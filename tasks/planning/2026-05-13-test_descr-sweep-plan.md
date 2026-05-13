# 20-commit sweep plan — essential conformance, focused on `test_descr.py`

**Date:** 2026-05-13
**Scope:** one block of 20 commits + 1 final doc-update commit
**Target:** unblock and shrink the failure count of the Essential
conformance tests, with `test_descr.py` as the headline.  Spans
multiple chat interactions; this document is the contract.

## Reference baselines

Before this block (per `docs/CPYTHON_CONFORMANCE.md` 2026-05-07
sweep):

| Test                  | Run / Fail / Error |
| :---                  | ---:               |
| `test_grammar.py`     | 75 / 0 / 1         |
| `test_types.py`       | 128 / 57 / 19      |
| **`test_descr.py`**   | **159 / 93 / 34**  |
| `test_generators.py`  | 60 / 60 / 22       |
| `test_base64.py`      | 39 / 17 / 17       |

Live blocker discovered while preparing this plan: `import signal`
fails with `KeyError` inside `enum._convert_` because
`sys.modules[__name__]` is consulted before the importer has
inserted the in-flight module into the module table.  Every
`unittest`-based test trips this because `unittest/signals.py`
imports `signal` at module load.  **Result: even running
`test_descr.py` is currently blocked.**  Fixing this is commit #1
of the block.

## Working rules for every commit

* **One root cause per commit** — never bundle unrelated fixes.
* **`ctest 199/199` green on every commit.** Run before pushing.
* **No `--no-verify`, no signed-bypass** — fix what the hook
  complains about.
* **Commit message format** mirrors recent sweep commits:
  * Headline (under 70 chars): area + verb + what.
  * Body: what diverged from CPython's contract, what the fix does,
    cross-reference to the relevant `test_descr.py` test method
    when applicable, ctest result.
  * Standard `Co-Authored-By: Claude Opus 4.7 (1M context)` trailer.
* **Each commit's body cites the test or CPython doc that motivated
  it**, so a reviewer can spot-check the divergence.

## Commit block

### Phase A — Unblock measurement (3 commits)

* **A-01.  `signal` module: insert into `sys.modules` before
  executing module body.**  CPython's importlib sets
  `sys.modules[name] = module` *before* running the module body so
  the module is observable from within its own init.  protoPython's
  importer inserts after.  signal.py's first non-trivial statement
  calls `enum._convert_('Signals', __name__, …)` which reads
  `sys.modules[__name__]` — that's why `unittest.signals.py`
  blows up importing `signal` and every test built on unittest
  cascades.  Fix: move the registration into the module slot to
  the start of import-finalisation, before `proto_module_init` /
  body execution.  Cross-check: re-run `import signal`, `import
  unittest`, both should print OK; `test_grammar.py` should
  collect.
* **A-02.  Conformity harness: skip blockers without
  silent-success.**  Update `tests/conformity/run_conformity.py`
  (or equivalent) so a CRASH at import is reported as
  IMPORT_FAIL rather than counted alongside per-test pass/fail.
  Without this, the next 18 commits can't tell whether
  test_descr's number moved.
* **A-03.  Establish a fresh baseline measurement of `test_descr.py`
  and pin it in `tasks/planning/test_descr_baseline_2026-05-13.md`.**
  Re-run unittest with the unblocked binary.  Capture full
  fail/error list with method names.  This document feeds every
  subsequent commit's "why this row".

### Phase B — OperatorsTest cluster (4 commits)

`test_descr.OperatorsTest` exercises every built-in numeric type's
unary/binary operator dispatch and the reverse-method protocol
(`__radd__` etc.).

* **B-04.  binaryOp dispatch: honour `__rop__` when `__op__`
  returns `NotImplemented` and types differ.**  CPython
  `PyNumber_Add` pattern.  Source: `OperatorsTest.test_explicit_reverse_methods`.
* **B-05.  `divmod` builtin dispatches through `__divmod__` /
  `__rdivmod__`.**  Source: `OperatorsTest.test_ints`,
  `OperatorsTest.test_floats` `divmod(a, b)` rows.
* **B-06.  `pow(a, b, c)` three-argument modular-pow path through
  `__pow__(self, exp, mod)`.**  Source: `OperatorsTest.test_ints`
  `pow(a, b, c)`.
* **B-07.  Numeric promotion: `int + float` returns a float via
  `__radd__` on float when `int.__add__` reports
  NotImplemented.**  CPython numeric tower contract.  Source:
  `OperatorsTest.test_explicit_reverse_methods`.

### Phase C — Descriptor protocol corner cases (5 commits)

`ClassPropertiesAndMethods` and `PicklingTests` are dominated by
descriptor / `__getattribute__` interactions.

* **C-08.  `object.__getattribute__` looks up a data descriptor on
  the type **before** falling back to instance dict, then non-data
  descriptor.**  Source: any `*_descriptor` test in
  `ClassPropertiesAndMethods`; tests around `slot_descriptor`.
* **C-09.  `property.__set__` / `__delete__` raise AttributeError
  with the correct property name embedded in the message.**
  Source: `ClassPropertiesAndMethods.test_dynamics` /
  `test_dictproxyiterkeys`.
* **C-10.  `classmethod.__get__(instance, owner)` returns a
  bound method whose `__self__` is `owner` (not `instance`).**
  Source: `ClassPropertiesAndMethods.test_classmethods`.
* **C-11.  `staticmethod.__get__` returns the underlying function
  unchanged, regardless of instance / owner.**  Source:
  `ClassPropertiesAndMethods.test_staticmethods`.
* **C-12.  `__slots__`: assigning to an attribute name not listed
  raises AttributeError with the slot-class's name embedded.**
  Source: `ClassPropertiesAndMethods.test_slots`,
  `test_slots_special`.

### Phase D — Pickling & copy protocol (4 commits)

`PicklingTests` is the largest single failing class in
test_descr.py (93F+34E mostly here).  CPython's
`object.__reduce_ex__` /  `object.__reduce__` chain is the
authoritative spec.

* **D-13.  `object.__reduce_ex__(protocol)` dispatches to
  `__reduce__` when protocol < 2 and to the
  `copyreg.__reduce_ex__` slot machinery otherwise.**  Source:
  `PicklingTests.test_reduce`.
* **D-14.  `object.__reduce__()` returns a 2-tuple
  `(copyreg._reconstructor, (cls, base, state))` matching CPython
  exact arity.**  Source: `PicklingTests.test_pickle_slots`.
* **D-15.  `copy.copy` / `copy.deepcopy` use `__deepcopy__` /
  `__copy__` when present on the instance, fall back to
  `__reduce_ex__(2)` otherwise.**  Source:
  `PicklingTests.test_copy_setstate`.
* **D-16.  `__getstate__` / `__setstate__` contract: missing
  `__getstate__` returns the instance `__dict__` (plus slots),
  missing `__setstate__` assigns the state dict to `__dict__` and
  copies slot values.**  Source:
  `PicklingTests.test_default_getstate_setstate`.

### Phase E — MRO / `__mro_entries__` / metaclass (2 commits)

* **E-17.  `__mro_entries__` honoured during class creation when
  a non-type appears in `bases`.**  Source: `MroTest.test_mro`
  variants.
* **E-18.  Metaclass conflict detection raises TypeError with the
  exact CPython message format.**  Source:
  `ClassPropertiesAndMethods.test_metaclass`,
  `MroTest.test_mro_disagreement`.

### Phase F — Adjacent essentials touched in passing (2 commits)

* **F-19.  `test_grammar.py:417`: PEP 649 deferred-annotation
  evaluation — wire `__annotate__` into function frame even when
  the function has no explicit annotations.**  Closes the single
  remaining error in test_grammar.
* **F-20.  `test_types.py` UnionType: parameter substitution on
  `int | str | T` honours the order of free variables (cluster
  of ~12 substitution-test failures share this root).**

### Phase G — Doc update (1 commit, total = 21)

* **G-21.  `docs/CPYTHON_CONFORMANCE.md`: add 2026-05-13
  post-eighth-sweep section.**  Re-measure the 19-test catalog,
  re-measure `test_descr.py` / `test_grammar.py` /
  `test_types.py` direct unittest counts, tabulate deltas vs the
  2026-05-07 baseline, summarise the 20 fixes by theme (same
  table format as the existing 2026-05-12 section), bump the
  cumulative count.  Pair with a CHANGELOG entry.

## Risk register

* **Blocker risk on A-01.**  The sys.modules insertion-order fix
  may have downstream effects (anything that relied on the
  observable "not yet registered" state).  Mitigation: run full
  ctest + run all conformity tests before claiming success.
* **D-13/14 may surface latent `__class__` / type-id machinery
  bugs**.  Mitigation: start with `__reduce__` on a hand-written
  pickle test before touching the test_descr.py path; this is
  effectively what every "PicklingTests" failure flushes out.
* **E-17 (`__mro_entries__`) interacts with class creation order**
  — if we land it without C-08/C-09 done first, the descriptor
  changes may cascade.  Mitigation: keep the phase ordering
  above, and re-run test_descr.py per phase boundary.

## Definition of done

* All 21 commits landed, each green under `ctest --test-dir
  build`.
* `test_descr.py` fail+error count strictly lower than the
  2026-05-13 baseline captured in A-03.
* `docs/CPYTHON_CONFORMANCE.md` updated with the new numbers and
  the per-theme summary.
* No `--no-verify`, no force-push, no skipped hooks across the
  block.

## How later interactions resume

Each chat session that continues the work should:

1. Read this file plus
   `tasks/planning/test_descr_baseline_2026-05-13.md` (created by
   A-03).
2. Identify the next un-committed item in the phase list above.
3. Reproduce the failing test method in isolation before writing
   the fix — never assume the failure description here is
   authoritative without rechecking.
4. Land one commit, ensure ctest 199/199, then loop.
