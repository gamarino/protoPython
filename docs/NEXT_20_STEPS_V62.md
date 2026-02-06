# protoPython: Next 20 Steps (v62) — Swarm Hardening and Lessons

Plan for steps 1165–1184. Block 1100-1200 V2 final.

**Prerequisite**: v61 completed (GetRawPointer API, Swarm tests).

**Theme**: Swarm test hardening; lessons v58–v62; final documentation updates; lock-free/C++20 notes.

**Status**: Completed.

---

## Steps 1165–1184

| Step | Description | Status |
|------|-------------|--------|
| 1165 | Create NEXT_20_STEPS_V62.md | done |
| 1166–1168 | Swarm test hardening: document DISABLED_OneMillionConcats / DISABLED_LargeRopeIndexAccess root causes; no hacks—follow-up for GC/rope on very large graphs | done |
| 1169–1170 | "Swarm of One" benchmark: document as future work (1M ProtoTuple-strings vs std::string); optional protoCore benchmark target | done |
| 1171–1172 | Lock-free / C++20: note in docs (protoCore no compaction, stable addresses; C++20 used in protoCore) | done |
| 1173–1176 | tasks/lessons.md: add v58–v62 lessons (ropes, inline strings, ProtoExternalBuffer, GetRawPointer, Swarm tests, OperatorInvert __call__ path) | done |
| 1177–1178 | IMPLEMENTATION_PLAN: v61 done, v62 in progress then completed | done |
| 1179–1180 | tasks/todo.md: v62 section; mark steps 1165–1184 complete | done |
| 1181–1182 | STUBS.md, TESTING.md: v62 final updates (Swarm disabled tests, lessons ref) | done |
| 1183–1184 | STRING_SUPPORT.md: v62 ref; commit v62 block | done |

---

## Summary

v62 closes the 1100–1200 V2 block. **Swarm hardening**: DISABLED_OneMillionConcats and DISABLED_LargeRopeIndexAccess remain disabled by design; root causes (GC segfault on very large rope graph, "Non-tuple object in tuple node slot") documented; fix requires protoCore GC and string-tuple traversal changes. **Swarm of One benchmark** (1M ProtoTuple-strings vs std::string) documented as optional/future. **Lessons v58–v62** captured in tasks/lessons.md. IMPLEMENTATION_PLAN, todo, STUBS, TESTING, STRING_SUPPORT updated. Lock-free/C++20 notes: protoCore does not compact; stable addresses; C++20 in use.
