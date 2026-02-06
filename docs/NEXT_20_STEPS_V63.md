# protoPython: Next 20 Steps (v63) — HPy Phase 1 Foundation

Plan for steps 1185–1204. First block of [NEXT_100_STEPS_HPY.md](NEXT_100_STEPS_HPY.md).

**Prerequisite:** v62 completed. [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md) and Phase 1 design (v55) in place.

**Theme:** HPy context, handle table, and core ABI (handles, GetAttr, SetAttr, Call, Type). No module loading yet.

**Status:** Pending.

---

## Steps 1185–1204

| Step | Description | Status |
|------|-------------|--------|
| 1185 | Create NEXT_20_STEPS_V63.md; add v63 section to tasks/todo.md | pending |
| 1186–1188 | Define HPy handle type and handle table (growable map: handle id → ProtoObject*); opaque HPy = index or pointer | pending |
| 1189–1190 | Implement HPyContext struct: handle table + ProtoContext*; one per thread or import session | pending |
| 1191–1192 | HPy_FromPyObject(ctx, obj): insert ProtoObject* into table, return handle | pending |
| 1193–1194 | HPy_AsPyObject(ctx, h): lookup handle, return ProtoObject* (or null) | pending |
| 1195–1196 | HPy_Dup(ctx, h): duplicate handle (ref-count or new slot); HPy_Close(ctx, h): release handle | pending |
| 1197–1198 | HPy_GetAttr(ctx, obj, name), HPy_SetAttr(ctx, obj, name, value) via ProtoObject get/setAttribute | pending |
| 1199–1200 | HPy_Call(ctx, callable, args): invoke via ProtoContext (call with __call__ or equivalent) | pending |
| 1201 | HPy_Type(ctx, obj): return type object (getPrototype) as handle | pending |
| 1202–1203 | C API header (e.g. include/protoPython/HPyContext.h or hpy_protopython.h) and single implementation file | pending |
| 1204 | Document v63 in IMPLEMENTATION_PLAN/STUBS; commit all new files and changes | pending |

---

## Deliverables (v63)

- **New files:** HPy context and handle table (header + .cpp or .c); optional minimal test.
- **API:** HPy_FromPyObject, HPy_AsPyObject, HPy_Dup, HPy_Close, HPy_GetAttr, HPy_SetAttr, HPy_Call, HPy_Type.
- **Doc:** NEXT_20_STEPS_V63.md (this file); todo.md v63; IMPLEMENTATION_PLAN/STUBS updated.
- **Commit:** `feat(hpy): Next 20 Steps v63 (1185–1204); HPy context, handle table, core ABI`

---

## Summary

v63 establishes the HPy Phase 1 foundation: a protoPython-specific HPy context with a handle table mapping HPy handles to ProtoObject*, and the minimal core ABI (handle lifecycle, attr, call, type). Module loading and HPyModuleDef are deferred to v64.
