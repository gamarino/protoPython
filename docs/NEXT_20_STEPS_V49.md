# protoPython: Next 20 Steps (v49)

Plan for 20 incremental milestones. Each step: implement → update docs → commit.

**Prerequisite**: Next 20 Steps (v48) completed (steps 885–904). See [NEXT_20_STEPS_V48.md](NEXT_20_STEPS_V48.md).

**Status**: In progress.

---

## Steps 905–924

| Step | Description | Status |
|------|-------------|--------|
| 905 | Create NEXT_20_STEPS_V49.md | done |
| 906–907 | Complete v48 doc updates: TESTING.md (remove known-issues if fixed), STUBS.md v48 section | done |
| 908–909 | Update tasks/todo.md v48 steps; IMPLEMENTATION_PLAN v48 summary | done |
| 910 | Fix OperatorInvert: root cause for operator.invert(5) returning nullptr; re-enable test | pending |
| 911 | Fix py_setattr: setattr(obj, "foo", 100) should persist; restore SetattrAndCallable to use setattr/getattr | pending |
| 912 | Fix py_log10: math.log10(100) returning nullptr in some contexts; restore MathLog to use log10 | pending |
| 913–914 | Fix ThreadModule: stack-use-after-scope in py_log_thread_ident (AddressSanitizer) | pending |
| 915–916 | Foundation tests: ensure full v48+v49 suite passes (SetBasic, MathLog, MathDist, OperatorInvert, SetattrAndCallable, MapBuiltin, StringDunders) | pending |
| 917–918 | Update TESTING.md: v49 known-issues; CTest gate if needed | pending |
| 919 | Update STUBS.md v49 section | pending |
| 920–921 | Update tasks/todo.md v49 steps | pending |
| 922–924 | Update IMPLEMENTATION_PLAN v49 summary; commit | pending |

---

## Summary

Steps 905–924 deliver: v48 doc completion (TESTING, STUBS, todo, IMPLEMENTATION_PLAN); fixes for OperatorInvert, py_setattr, py_log10; ThreadModule stack-use-after-scope fix; foundation test suite green; v49 docs and single commit.
