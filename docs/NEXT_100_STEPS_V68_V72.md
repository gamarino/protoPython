# Master Plan: Next 100 Steps (v68–v72)

This plan covers the transition from HPy completion to a high-quality REPL and final project refinements.

## Block v68: HPy Finalization & Industrialization (1285–1304)
*Focus: Robustness, error handling, and complete user journey.*
- [ ] 1285: Implement detailed error reporting in `HPyModuleProvider`.
- [ ] 1286: Add support for `HPy_ABI_VERSION` check in all loaders.
- [ ] 1287: Audit `HPyContext` for potential handle leaks in edge cases.
- [ ] 1288: Implement `HPy_AsLongLong` and `HPy_AsDouble` reliably.
- [ ] 1289: Add support for constant folding in HPy attribute access.
- [ ] 1290: Document the relationship between protoCore GC and HPy handles.
- [ ] 1291: Create a "Stress Test" script for HPy module loading/unloading.
- [ ] 1292: Refine `HPyModule_AddObject` to handle prototype inheritance correctly.
- [ ] 1293: Implement `HPy_Dump` for debugging HPy objects.
- [ ] 1294: Add support for HPy modules to define new exception types.
- [ ] 1295–1304: Complete HPy Phase 4 edge cases and sync all HPy docs.

## Block v69: REPL Foundation & Line Editing (1305–1324)
*Focus: Creating a usable interactive shell.*
- [ ] 1305: Integrate a line-editing library (e.g., `linenoise` or custom shim).
- [ ] 1306: Implement basic input loop for the `protopy` CLI.
- [ ] 1307: Support multiline input detection (matching parens/braces).
- [ ] 1308: Implement execution of partial code blocks.
- [ ] 1309: Add "interactive" flag to `PythonEnvironment`.
- [ ] 1310: Handle `EOF` (Ctrl-D) and `KeyboardInterrupt` (Ctrl-C) gracefully.
- [ ] 1311: Implement command history saving/loading.
- [ ] 1312: Add basic tab-completion for built-in names.
- [ ] 1324: Finalize the interactive sub-system foundation.

## Block v70: High-Quality Error Reporting (1325–1344)
*Focus: User-friendly feedback and CPython-level parity.*
- [ ] 1325: Implement structured exception formatting in the REPL.
- [ ] 1326: Add line-pointer indicators for syntax errors (the `^` marker).
- [ ] 1327: Implement colorized error messages (red for exceptions, cyan for types).
- [ ] 1328: Add "Did you mean?" suggestions for NameErrors.
- [ ] 1329: Implement a traceback formatter that respects the REPL's virtual file state.
- [ ] 1330: Ensure all `protoCore` errors are translated to Python exceptions.
- [ ] 1344: Finalize the error reporting engine.

## Block v71: Advanced REPL Features (1345–1364)
*Focus: UX and productivity.*
- [ ] 1345: Implement auto-indentation in multiline mode.
- [ ] 1346: Add syntax highlighting for the input line.
- [ ] 1347: Implement `help()` and `dir()` integration for native objects.
- [ ] 1348: Support the `PYTHONSTARTUP` equivalent for `protopy`.
- [ ] 1349: Add a `%debug` magic-like facility for inspection.
- [ ] 1364: Finalize the advanced interactive experience.

## Block v72: Final Integration & Pending Points (1365–1384)
*Focus: Overall project polish and completion.*
- [ ] 1365: Performance audit of the module resolution chain.
- [ ] 1366: Ensure CLI parity with `python3` for `-c`, `-m`, and `-p` flags.
- [ ] 1367: Document the full HPy + REPL integration.
- [ ] 1368: Address all `// TODO` items remaining in `PythonEnvironment.cpp`.
- [ ] 1384: Project-wide sync, final v72 commit, and "Version 1.0 Alpha" milestone.

---
*Updated on 2026-02-06 based on priority for HPy User Guide and CPython-equivalent REPL.*
