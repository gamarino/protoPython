# protoPython: Next 20 Steps (v49)

Plan for 20 incremental milestones. Each step: implement → update docs → commit.

**Prerequisite**: Next 20 Steps (v48) completed (steps 885–904). See [NEXT_20_STEPS_V48.md](NEXT_20_STEPS_V48.md).

**Status**: In progress.

---

## Steps 905–924

| Step | Description | Status |
|------|-------------|--------|
| 905 | Create NEXT_20_STEPS_V49.md | done |
| 906–907 | Complete v48 doc updates: TESTING.md (known-issues), STUBS.md v48 section | done |
| 908–909 | Complete v48: tasks/todo.md v48 steps, IMPLEMENTATION_PLAN v48 summary | done |
| 910–911 | Fix OperatorInvert: direct asMethod returns nullptr; Python path works (DISABLED_) | deferred |
| 912–913 | Fix py_setattr: setattr(obj, "foo", 100) should persist for getattr | pending |
| 914–915 | Fix py_log10: math.log10(100) returning nullptr (restore direct log10 call) | pending |
| 916–917 | Fix ThreadModule: stack-use-after-scope in py_log_thread_ident | pending |
| 918–919 | Re-enable OperatorInvert test when fix is applied | pending |
| 920–921 | Update SetattrAndCallable test to use py_setattr when fixed | pending |
| 922–924 | Update TESTING, STUBS, todo, IMPLEMENTATION_PLAN; commit | pending |

---

## Summary

Steps 905–924 deliver: v48 doc completion; OperatorInvert, py_setattr, py_log10, ThreadModule fixes; test restorations; TESTING, STUBS, todo, IMPLEMENTATION_PLAN updated; single commit.
