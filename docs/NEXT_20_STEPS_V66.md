# Next 20 Steps (v66)

These steps (1245–1264) focus on HPy Phase 3: Advanced API coverage (collections, rich comparisons, iterators, and type creation).

| Step | Status | Task | Summary |
|---|---|---|---|
| 1245 | [x] | Bitwise Protocols | Implement `HPy_And`, `HPy_Or`, `HPy_Xor`, `HPy_LShift`, `HPy_RShift`. |
| 1246 | [x] | Rich Comparison | Implement `HPy_RichCompare` and comparison constants (LT, LE, EQ, etc.). |
| 1247 | [x] | List Builder API | Implement `HPyList_New`, `HPyList_Append`, and index-based access. |
| 1248 | [x] | Dict Builder API | Implement `HPyDict_New` and mapping setters/getters. |
| 1249 | [x] | Tuple ABI | Implement `HPyTuple_New` and `HPyTuple_Pack`. |
| 1250 | [x] | List Builder (Manual) | Implement `HPyListBuilder` (Simplified). |
| 1251 | [x] | Tuple Builder | Implement `HPyTuple_Builder` (Simplified). |
| 1252 | [x] | Iterator ABI | Implement `HPy_GetIter` and `HPy_Next`. |
| 1253 | [x] | Slice Support | Implement `HPySlice_New` for indexing. |
| 1254 | [x] | Long Long Conversion | Implement `HPyLong_AsLongLong` and `HPyLong_FromLongLong`. |
| 1255 | [x] | Type Creation (Part 1) | Define `HPyType_Spec` and `HPyType_Slot`. |
| 1256 | [x] | Type Creation (Part 2) | Implement `HPyType_FromSpec` to define new HPy types. |
| 1257 | [x] | Global Variable ABI | Implement `HPyModule_AddStringConstant` and constants. |
| 1258 | [x] | Call Refinement | Implement `HPy_CallMethod` helper. |
| 1259 | [x] | Exception Check ABI | Implement `HPyErr_Occurred` and `HPyErr_Fetch` (Stubs). |
| 1260 | [x] | Memory Mapping Helper | Implement `HPyBytes_AsString` and size (Stubs). |
| 1261 | [x] | Ref count Stubs | Implement `HPy_INCREF`/`DECREF` as no-ops. |
| 1262 | [x] | TODO Update | Sync `tasks/todo.md` with v66 completion. |
| 1263 | [x] | Implementation Plan Sync | Update `docs/IMPLEMENTATION_PLAN.md` with v66 completion. |
| 1264 | [x] | Commit V66 | Documented and finalized the v66 implementation block. |

---
*Generated based on HPY_INTEGRATION_PLAN.md and NEXT_100_STEPS_HPY.md.*
