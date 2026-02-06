# protoPython: Next 20 Steps (v59) — Clustering and index by tuple traverse

Plan for steps 1105–1124. Block 1100-1200 V2 continued.

**Prerequisite**: v58 completed (inline strings, O(1) concat via ProtoTuple).

**Theme**: Character clustering in 64-byte tuple cells; index access by traversing ProtoTuple hierarchy; iterator O(1) amortized.

**Status**: In progress.

---

## Steps 1105–1124

| Step | Description | Status |
|------|-------------|--------|
| 1105 | Create NEXT_20_STEPS_V59.md | done |
| 1106–1108 | Clustering: each 64-byte tuple node holds cluster of UTF-32 code points (slot[i] = embedded char or small tuple); cache locality | done |
| 1109–1110 | getAt(index) by traversing tuple tree — descend by length (left/right for concat); O(depth) then O(1) at leaf | done |
| 1111–1112 | getSize: concat tuple = stored total length; leaf = actual_size | done |
| 1113–1114 | Iterator: next() advances through tuple hierarchy; O(1) amortized per character (iterator holds string + index, uses getProtoStringGetAt) | done |
| 1115–1116 | tasks/todo.md v59; IMPLEMENTATION_PLAN v58 done, v59 in progress | done |
| 1117–1120 | STUBS.md, TESTING.md; STRING_SUPPORT.md (ropes as ProtoTuple, inline, indexing) | done |
| 1121–1124 | Commit: NEXT_20_STEPS_V59, docs, todo, IMPLEMENTATION_PLAN, STUBS, TESTING | pending |

---

## Summary

v59 relies on v58: concat tuples (2 slots) and leaf tuples (1..TUPLE_SIZE slots) already give clustering; getAt and getSize implemented in v58; iterator uses unified getProtoStringSize/getProtoStringGetAt. Steps 1115–1124: doc updates and commit.
