# protoPython: Next 20 Steps (v10)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v9) completed (steps 105–124).

**Status**: Completed.

---

## Steps 125–144

| Step | Description | Commit message |
|------|-------------|----------------|
| 125 | Builtin `divmod(a, b)` | done |
| 126 | Builtin `ascii(obj)` | done |
| 127 | Execution engine: `STORE_SUBSCR` (subscript assignment) | done |
| 128 | `str.count(sub[, start[, end]])` | done |
| 129 | `list.reverse()` method | done |
| 130 | `dict.pop(key[, default])` | done |
| 131 | `typing` stub: `Optional`, `Union` placeholder | done |
| 132 | `hashlib` stub (md5, sha1 placeholder) | done |
| 133 | `copy` module stub (copy.copy, copy.deepcopy) | done |
| 134 | `itertools.chain` | done |
| 135 | STORE_SUBSCR: doc note (no dedicated CTest) | done |
| 136 | `int.bit_length()` on int prototype | done |
| 137 | `str.rsplit(sep=None, maxsplit=-1)` | done |
| 138 | Foundation test for `filter` builtin | done |
| 139 | Update IMPLEMENTATION_PLAN Phase 3 opcode list | done |
| 140 | STORE_SUBSCR coverage (see 135) | done |
| 141 | Document v10 scope in IMPLEMENTATION_PLAN | done |
| 142 | Update todo and NEXT_20_STEPS_V10 for v10 completion | done |
| 143 | Mark v10 complete in todo | done |
| 144 | Final v10 review and commit any remaining docs | done |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md` (and this file for v10).
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R protopy_cli` when runtime/CLI code changes.
