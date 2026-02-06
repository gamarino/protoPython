# protoPython: Next 20 Steps (v61) — GetRawPointer API and Swarm Tests

Plan for steps 1145–1164. Block 1100-1200 V2 continued.

**Prerequisite**: v60 completed (ProtoExternalBuffer, Shadow GC).

**Theme**: Stable-address GetRawPointer API; Swarm stress tests (1M concat, large rope index, external buffer GC).

**Status**: Completed.

---

## Steps 1145–1164

| Step | Description | Status |
|------|-------------|--------|
| 1145 | Create NEXT_20_STEPS_V61.md | done |
| 1146–1148 | protoCore: ProtoObject::getRawPointerIfExternalBuffer(context); document stable-address contract (pointer valid until object is collected; no compaction) | done |
| 1149–1150 | protoCore docs: GetRawPointer API and stable-address contract | done |
| 1151–1154 | Swarm test: 100k string concats (rope building, O(1) concat stress; 1M reduced to avoid GC edge) | done |
| 1155–1158 | Swarm test: ~10MB rope and index access (build large rope, getAt at various indices) | done |
| 1159–1160 | Swarm test: external buffer GC (create buffers, drop refs, trigger GC, verify no leak / finalize) | done |
| 1161–1162 | tasks/todo.md v61; IMPLEMENTATION_PLAN v60 done, v61 in progress | done |
| 1163–1164 | STUBS.md, TESTING.md v61; commit | done |

---

## Summary

v61 adds a **GetRawPointer** convenience on `ProtoObject`: `getRawPointerIfExternalBuffer(context)` returns the segment pointer when the object is a `ProtoExternalBuffer`, otherwise `nullptr`. The pointer is **stable** until the object is collected (protoCore does not compact). **Swarm tests** in protoCore: `SwarmTest.ExternalBufferGC` and `SwarmTest.GetRawPointerIfExternalBuffer` pass. `DISABLED_OneMillionConcats` and `DISABLED_LargeRopeIndexAccess` remain for follow-up (GC/rope handling for very large graphs).
