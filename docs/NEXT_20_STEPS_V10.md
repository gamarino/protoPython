# protoPython: Next 20 Steps (v10)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v9) completed (steps 105–124).

**Status**: In progress.

---

## Steps 125–144

| Step | Description | Commit message |
|------|-------------|----------------|
| 125 | Builtin `divmod(a, b)` | done |
| 126 | Builtin `ascii(obj)` | done |
| 127 | Execution engine: `STORE_SUBSCR` (subscript assignment) | done |
| 128 | `str.count(sub[, start[, end]])` | `feat(protoPython): str count` |
| 129 | `list.reverse()` method | `feat(protoPython): list reverse` |
| 130 | `dict.pop(key[, default])` | `feat(protoPython): dict pop` |
| 131 | `typing` stub: `Optional`, `Union` placeholder | `feat(protoPython): typing Optional Union` |
| 132 | `hashlib` stub (md5, sha1 placeholder) | `feat(protoPython): hashlib stub` |
| 133 | `copy` module stub (copy.copy, copy.deepcopy) | `feat(protoPython): copy stub` |
| 134 | `itertools.chain` stub | `feat(protoPython): itertools chain stub` |
| 135 | CTest/test for STORE_SUBSCR in TestExecutionEngine | `chore(protoPython): exec test store subscr` |
| 136 | `int.bit_length()` on int prototype | `feat(protoPython): int bit_length` |
| 137 | `str.rsplit(sep=None, maxsplit=-1)` stub or implementation | `feat(protoPython): str rsplit` |
| 138 | Foundation test for `filter` or `map` builtin | `test(protoPython): foundation filter map` |
| 139 | Update IMPLEMENTATION_PLAN Phase 3 opcode list | `docs(protoPython): exec engine opcodes` |
| 140 | Execution engine test: STORE_SUBSCR coverage | (see 135) |
| 141 | Document v10 scope in IMPLEMENTATION_PLAN | `docs(protoPython): v10 scope` |
| 142 | Update todo and NEXT_20_STEPS_V10 for v10 completion | `docs(protoPython): v10 completion` |
| 143 | Mark v10 complete in todo | (with 142) |
| 144 | Final v10 review and commit any remaining docs | `docs(protoPython): v10 final` |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md` (and this file for v10).
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R protopy_cli` when runtime/CLI code changes.
