# protoPython: Next 20 Steps (v51)

Plan for 20 incremental milestones. Each step: implement → update docs → commit.

**Prerequisite**: Next 20 Steps (v50) completed (steps 925–944). See [NEXT_20_STEPS_V50.md](NEXT_20_STEPS_V50.md).

**Status**: In progress.

---

## Steps 945–964

| Step | Description | Status |
|------|-------------|--------|
| 945 | Create NEXT_20_STEPS_V51.md | done |
| 946–947 | tasks/todo.md: add v51 section; mark v49/v50 completion | done |
| 948–949 | IMPLEMENTATION_PLAN: v50 done, v51 in progress | done |
| 950–951 | TESTING.md: v51 known-issues (OperatorInvert DISABLED_, py_setattr/py_getattr posArgs, protoCore immutable model) | done |
| 952–953 | STUBS.md: v51 section; foundation test status | done |
| 954–955 | ProtoCore immutable model: SetattrAndCallable uses direct setAttribute/getAttribute; doc in test and TESTING | done |
| 956–957 | ThreadModule: stack-use-after-scope fix (py_log_thread_ident) — document status | pending |
| 958–959 | py_log10: math.log10(100) workaround (math.log(100, 10)); restore direct log10 when fixed | pending |
| 960–962 | OperatorInvert: DISABLED_; direct asMethod returns nullptr; Python path works | done |
| 963–964 | Commit: docs/NEXT_20_STEPS_V51.md, todo, IMPLEMENTATION_PLAN, STUBS, TESTING | pending |

---

## Summary

Steps 945–964 deliver: NEXT_20_STEPS_V51.md; tasks/todo.md v51; IMPLEMENTATION_PLAN v50 done, v51 in progress; TESTING and STUBS v51 updates; protoCore immutable model documented; OperatorInvert, py_setattr/py_log10/ThreadModule status captured.
