# protoPython: Next 20 Steps (v36)

Plan for 20 incremental milestones. Each step: implement → update docs → `git add` → commit.

**Prerequisite**: Next 20 Steps (v35) completed (steps 625–644). See [NEXT_20_STEPS_V35.md](NEXT_20_STEPS_V35.md).

**Status**: Completed.

---

## Steps 645–664

| Step | Description | Status |
|------|-------------|--------|
| 645 | Create NEXT_20_STEPS_V36.md, update IMPLEMENTATION_PLAN | done |
| 646 | Concurrent execution model subsection (GIL_FREE_AUDIT) | done |
| 647 | ProtoCore compatibility (INSTALLATION) | done |
| 648 | STUBS.md v36 section (Reserved / New stubs v36) | done |
| 649 | Update STUBS.md with v36 entries | done |
| 650 | Update tasks/todo.md for v36 | done |
| 651 | Finalize NEXT_20_STEPS_V36.md | done |
| 652 | IMPLEMENTATION_PLAN v36 completion | done |
| 653 | TESTING.md note for v36 | done |
| 654–663 | Reserved / finalize docs | done |
| 664 | Git add, commit | done |

---

## Summary

Steps 645–664 delivered: "Concurrent execution model" subsection in GIL_FREE_AUDIT (shared env/context safe for trace, resolve, context map; allocation/GC require per-thread ProtoThread or serialized execution; current test serializes compile+run); "ProtoCore compatibility" subsection in INSTALLATION (sibling directory, version not pinned, recommend known commit for reproducibility); STUBS.md v36 section (reserved); IMPLEMENTATION_PLAN, tasks/todo.md, and TESTING.md updated; v36 doc finalized; single commit.
