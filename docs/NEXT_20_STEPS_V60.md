# protoPython: Next 20 Steps (v60) — ProtoExternalBuffer and Shadow GC

Plan for steps 1125–1144. Block 1100-1200 V2 continued.

**Prerequisite**: v59 completed (ropes as ProtoTuple, getAt by tuple traverse, iterator).

**Theme**: External raw-memory buffers with 64-byte header; Shadow GC finalizes segment on collection.

**Status**: Completed.

---

## Steps 1125–1144

| Step | Description | Status |
|------|-------------|--------|
| 1125 | Create NEXT_20_STEPS_V60.md | done |
| 1126–1128 | ProtoExternalBuffer: 64-byte header cell; segment via std::aligned_alloc(64, size); processReferences empty; finalize() frees segment | done |
| 1129–1130 | proto_internal.h: BigCell union, ExpectedTag, POINTER_TAG_EXTERNAL_BUFFER; ProtoExternalBuffer API in protoCore.h | done |
| 1131–1132 | ProtoContext::newExternalBuffer(size); Proto.cpp getPrototype, asExternalBuffer | done |
| 1133–1134 | CMakeLists.txt includes core/ProtoExternalBuffer.cpp; protoCore build | done |
| 1135–1136 | Run protoCore tests (string/primitives; newExternalBuffer/GC where applicable) | done |
| 1137–1138 | protoCore docs: ProtoExternalBuffer and Shadow GC (finalize frees segment when cell is collected) | done |
| 1139–1140 | tasks/todo.md v60; IMPLEMENTATION_PLAN v59 done, v60 in progress | done |
| 1141–1142 | STUBS.md, TESTING.md v60 updates | done |
| 1143–1144 | Commit: NEXT_20_STEPS_V60, protoCore ProtoExternalBuffer + Shadow GC, todo, IMPLEMENTATION_PLAN, STUBS, TESTING | done |

---

## Summary

v60 adds **ProtoExternalBuffer**: a 64-byte header cell holding a contiguous segment allocated with `std::aligned_alloc(64, bufferSize)`. The segment is not traced by the GC (`processReferences` is empty). When the cell is collected, **Shadow GC** runs `finalize()`, which frees the segment and sets the pointer to nullptr. API: `ProtoContext::newExternalBuffer(size)`, `ProtoExternalBuffer::getRawPointer(context)`, `getSize(context)`, `asObject(context)`, `getHash(context)`. proto_internal.h BigCell and ExpectedTag updated; Proto.cpp and ProtoContext already wired. CMakeLists includes ProtoExternalBuffer.cpp; build and tests pass.
