# protoPython: Next 20 Steps (v58) — Implementation Block 1100-1200 V2

Plan for 20 incremental milestones (steps 1085–1104). Each step: implement, update docs, commit.

**Prerequisite**: Next 20 Steps (v57) completed (steps 1065–1084).

**Theme**: Strings as ProtoTuple only; O(1) concatenation via concat tuple (left, right); inline string optimization (up to 7 logical UTF-32 chars in tagged pointer).

**Status**: In progress.

---

## Steps 1085–1104

| Step | Description | Status |
|------|-------------|--------|
| 1085 | Create NEXT_20_STEPS_V58.md for block 1100-1200 V2 | done |
| 1086–1087 | Design doc: Strings as ProtoTuple only — concat = new ProtoTuple(left, right, totalLength); leaf = tuple of chars | done |
| 1088–1089 | Inline string: Encoding for up to 7 UTF-32 chars in tagged pointer (EMBEDDED_TYPE_INLINE_STRING) | done |
| 1090–1091 | protoCore: Extend tagging and ProtoString path to detect inline string; getSize/getAt/iterator delegate to inline payload | done |
| 1092–1093 | protoCore: ProtoString concatenation via ProtoTuple — factory for one ProtoTuple(left, right, size); O(1) | done |
| 1094–1095 | protoPython: tasks/todo.md v58; IMPLEMENTATION_PLAN v57 done, v58 in progress | done |
| 1096–1098 | STUBS.md, TESTING.md: v58 (string/rope as ProtoTuple, inline) | done |
| 1099–1102 | protoCore: Document tuple convention for rope (concat vs leaf) and 64-byte cell layout; GC processReferences | done |
| 1103–1104 | Commit: NEXT_20_STEPS_V58, design doc, protoCore string/inline + ProtoTuple concat, todo, IMPLEMENTATION_PLAN, STUBS, TESTING | pending |

---

## Summary

Steps 1085–1104 deliver: NEXT_20_STEPS_V58.md; design doc (ropes as ProtoTuple); inline string encoding; protoCore ProtoString path for inline and O(1) concat via ProtoTuple; docs and commit.
