# protoPython: Next 20 Steps (v11)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v10) completed (steps 125–144).

**Status**: Completed.

---

## Steps 145–164

| Step | Description | Commit message |
|------|-------------|----------------|
| 145 | Builtin `ord(c)` | done |
| 146 | Builtin `chr(i)` | done |
| 147 | Builtin `bin(i)` | done |
| 148 | Builtins `oct(i)`, `hex(i)` | done |
| 149 | `list.sort()` method | done |
| 150 | Execution engine: BUILD_TUPLE | done |
| 151 | Execution engine: GET_ITER | done |
| 152 | Execution engine: FOR_ITER | done |
| 153 | argparse stub | done |
| 154 | getopt stub | done |
| 155 | tempfile stub | done |
| 156 | subprocess stub | done |
| 157 | str.encode, bytes.decode | done |
| 158 | float.is_integer() | done |
| 159 | Foundation test for map builtin | done |
| 160 | Execution engine test BUILD_TUPLE | done |
| 161 | GET_ITER/FOR_ITER test or doc | done |
| 162 | Update IMPLEMENTATION_PLAN Phase 3 and v11 scope | done |
| 163 | Create NEXT_20_STEPS_V11.md | done |
| 164 | Update todo for v11 completion | done |

---

## Execution workflow per step

1. Implement code (include tests where applicable).
2. Update `docs/IMPLEMENTATION_PLAN.md` and `tasks/todo.md` (and this file for v11).
3. Add any step-specific doc if needed.
4. `git add` all touched files.
5. Commit with the suggested message.

Run `test_foundation_filtered` and `ctest -R protopy_cli` when runtime/CLI code changes.
