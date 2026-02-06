# Next 20 Steps (v65)

These steps (1225–1244) focus on HPy Phase 3: API coverage (object creation, protocols, exceptions) and finalizing Phase 2 (Universal ABI) wiring for protoPython.

| Step | Status | Task | Summary |
|---|---|---|---|
| 1225 | [x] | HPy_New | Implement basic object allocation from a type. |
| 1226 | [x] | HPy_FromNumeric | Implement `HPy_FromLong` and `HPy_FromDouble`. |
| 1227 | [x] | HPy_FromUTF8 | Implement string creation from UTF-8. |
| 1228 | [x] | HPy_AsNumeric | Implement `HPy_AsLong` and `HPy_AsDouble`. |
| 1229 | [x] | HPy_AsUTF8 | Implement string/buffer access in HPy. |
| 1230 | [x] | HPyErr_SetString | Implement exception raising with a message. |
| 1231 | [x] | HPyErr_Occurred | Implement exception check ABI (stubbed). |
| 1232 | [x] | HPyErr_Clear | Implement exception clear ABI (stubbed). |
| 1233 | [x] | Number Protocol (Part 1) | Implement `HPy_Add` and `HPy_Subtract`. |
| 1234 | [x] | Number Protocol (Part 2) | Implement `HPy_Multiply` and `HPy_TrueDivide`. |
| 1235 | [x] | Sequence Protocol | Implement `HPy_GetItem` and `HPy_SetItem`. |
| 1236 | [x] | HPy_Length | Implement length query for sequences/mappings. |
| 1237 | [x] | HPy_Contains | Implement containment check protocol. |
| 1238 | [x] | HPy_IsTrue | Implement truth value testing. |
| 1239 | [x] | Static Attr ABI | Implement `HPy_GetAttr_s` and `HPy_SetAttr_s`. |
| 1240 | [x] | HPyModule_AddObject | Helper for adding objects/types to a module. |
| 1241 | [x] | Import Refinement | Ensure HPy modules are correctly handled in sys.modules. |
| 1242 | [x] | TODO Update | Sync `tasks/todo.md` with v65 completion. |
| 1243 | [x] | Implementation Plan Sync | Update `docs/IMPLEMENTATION_PLAN.md` with v65 completion. |
| 1244 | [x] | Commit V65 | Documented and finalized the v65 implementation block. |

---
*Generated based on HPY_INTEGRATION_PLAN.md and NEXT_100_STEPS_HPY.md.*
