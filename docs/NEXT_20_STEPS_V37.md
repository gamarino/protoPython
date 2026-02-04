# protoPython: Next 20 Steps (v37)

Plan for 20 incremental milestones. Each step: implement → update docs → commit.

**Prerequisite**: Next 20 Steps (v36) completed (steps 645–664). See [NEXT_20_STEPS_V36.md](NEXT_20_STEPS_V36.md).

**Status**: Completed.

---

## Steps 665–684

| Step | Description | Status |
|------|-------------|--------|
| 665 | Create NEXT_20_STEPS_V37.md | done |
| 666 | atexit: register, unregister, _run_exitfuncs | done |
| 667 | Wire atexit._run_exitfuncs into runtime shutdown | done |
| 668 | shutil.copyfile using open/read/write | done |
| 669 | shutil.copy (delegate to copyfile for files) | done |
| 670 | traceback: extract_tb, format_list, format_exception | done |
| 671 | difflib.unified_diff basic implementation | done |
| 672 | dataclasses.dataclass minimal __init__ from annotations | done |
| 673 | contextlib.contextmanager generator-based | done |
| 674 | unittest.TestCase: assertEqual, assertTrue, assertFalse, fail | done |
| 675 | unittest.main minimal runner | done |
| 676 | Foundation test AtexitRegister | done |
| 677 | Foundation test ShutilCopyfile | done |
| 678 | Update STUBS.md v37 entries | done |
| 679 | Update tasks/todo.md for v37 | done |
| 680 | Update IMPLEMENTATION_PLAN v37 | done |
| 681–683 | Reserved | done |
| 684 | Git add, commit | pending |

---

## Summary

Steps 665–684 will deliver: atexit with register/unregister/_run_exitfuncs wired to runtime shutdown; shutil.copyfile and copy; traceback extract_tb, format_list, format_exception; difflib.unified_diff; dataclasses.dataclass minimal __init__; contextlib.contextmanager; unittest.TestCase basics and main; foundation tests; STUBS, todo, IMPLEMENTATION_PLAN updated; single commit.
