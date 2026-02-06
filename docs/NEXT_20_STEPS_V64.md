# Next 20 Steps (v64)

These steps (1205–1224) focus on Phase 1 completion (module loading) and Phase 2 start (Universal ABI) for HPy support in protoPython.

| Step | Status | Task | Summary |
|---|---|---|---|
| 1205 | [x] | HPy Module Loader | Implement logic to load `.so` and `.hpy.so` files for HPy extensions. |
| 1206 | [x] | HPyModuleDef Resolution | Add support for resolving `HPyModuleDef` from loaded modules. |
| 1207 | [x] | HPy Init Invocation | Safely invoke the HPy initialization function for modules. |
| 1208 | [x] | Universal ABI Header | Define the base header requirements for HPy Universal ABI. |
| 1209 | [x] | ABI Entry Point | Implement the entry point for universal ABI module loading. |
| 1210 | [x] | Module Wiring | Wire HPy-loaded objects into the `PythonEnvironment` module map. |
| 1211 | [x] | Handle Recycling | Implement handle table recycling for better scalability. |
| 1212 | [x] | HPyMethodDef Support | Define structures for HPy module functions and methods. |
| 1213 | [x] | Method Wrapper | Implement C++ bridge to call HPy functions from ProtoObject. |
| 1214 | [x] | HPyModule_Create | Implement the ABI for dynamic module creation. |
| 1215 | [x] | Method Population | Automatically populate module attributes from `HPyMethodDef`. |
| 1216 | [x] | ABI Version Check | Implement checks for HPy API version compatibility. |
| 1217 | [x] | Handle Table Integration | Ensure the loader correctly initializes the handle table. |
| 1218 | [x] | Module Shutdown Hooks | Destructor handles basic library closing. |
| 1219 | [x] | Import Hook Logic | Integrated via `ProviderRegistry` and `PythonEnvironment`. |
| 1220 | [x] | CLI Flags for HPy | Added `PROTO_HPY_DEBUG` for tracing. |
| 1221 | [x] | Lessons Captured | Dynamic loading with HPy and ProtoObject is viable. |
| 1222 | [x] | TODO Update | Sync `tasks/todo.md` with v64 completion. |
| 1223 | [x] | Implementation Plan Sync | Update `docs/IMPLEMENTATION_PLAN.md` with v64 completion. |
| 1224 | [x] | Commit V64 | Documented the v64 implementation block. |

---
*Generated based on HPY_INTEGRATION_PLAN.md and NEXT_100_STEPS_HPY.md.*
