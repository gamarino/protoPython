# protoPython: Next 20 Steps (v9)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v8) completed (steps 85–104).

**Status**: In progress.

---

## Steps 105–124

| Step | Description | Commit message |
|------|-------------|----------------|
| 105 | Builtin `filter(function, iterable)` | done |
| 106 | Builtin `map(function, iterable)` (single iterable) | `feat(protoPython): map builtin` |
| 107 | Builtin `next(iter, default=None)` with optional second arg | `feat(protoPython): next default` |
| 108 | Execution engine: `BINARY_SUBSCR` (subscript) | `feat(protoPython): exec binary subscr` |
| 109 | Execution engine: `BUILD_MAP` (dict from stack) | `feat(protoPython): exec build map` |
| 110 | Fix or document `list.extend(iterable)` for non-list iterables | `fix(protoPython): list extend iterable` |
| 111 | `typing` stub: `Any`, `List`, `Dict` (type hints) | `feat(protoPython): typing stub` |
| 112 | `dataclasses` stub: `dataclass` decorator placeholder | `feat(protoPython): dataclasses stub` |
| 113 | `unittest` stub: `TestCase`, `main` placeholder | `feat(protoPython): unittest stub` |
| 114 | CTest: add regrtest run with `--output` to results dir | `chore(protoPython): ctest regrtest output` |
| 115 | Builtin `slice()` constructor | `feat(protoPython): slice builtin` |
| 116 | `str.find(sub)` and `str.index(sub)` on str prototype | `feat(protoPython): str find index` |
| 117 | `os.path.realpath`, `normpath` stubs | `feat(protoPython): os.path realpath normpath` |
| 118 | Execution engine test: BUILD_LIST in TestExecutionEngine (if not covered in 102) | already in 102 or `chore(protoPython): exec test build list` |
| 119 | Builtin `isinstance` / `issubclass` extended for common ABCs | `feat(protoPython): isinstance abc` |
| 120 | `functools.wraps` stub | `feat(protoPython): functools wraps stub` |
| 121 | `contextlib` stub: `contextmanager` placeholder | `feat(protoPython): contextlib stub` |
| 122 | Foundation test for `sorted()` builtin | `test(protoPython): foundation sorted` |
| 123 | Document v9 scope in IMPLEMENTATION_PLAN | `docs(protoPython): v9 scope` |
| 124 | Update IMPLEMENTATION_PLAN and todo for v9 completion | `docs(protoPython): v9 completion` |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md` (and this file for v9).
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R protopy_cli` when runtime/CLI code changes.
