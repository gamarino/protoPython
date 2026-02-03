# protoPython Task List

Derived from [IMPLEMENTATION_PLAN.md](../docs/IMPLEMENTATION_PLAN.md). See also [DESIGN.md](../DESIGN.md) and workspace [tasks/todo.md](../../tasks/todo.md).

## 1. Core Runtime & Object Model
- [ ] Phase 2: Full Method Coverage — all dunder methods for built-in types; first batch (list/dict `__getitem__`, `__setitem__`, `__len__`) [x]
- [ ] Phase 3: Execution Engine — ProtoContext integration for bytecode execution (protopy)
- [x] Phase 4: GIL-less Concurrency — audit documented in [docs/GIL_FREE_AUDIT.md](../docs/GIL_FREE_AUDIT.md); fixes TBD

## 2. Standard Library Integration
- [x] Phase 2: Identify remaining C modules to replace (list in [docs/C_MODULES_TO_REPLACE.md](../docs/C_MODULES_TO_REPLACE.md))
- [ ] Phase 3: Native Optimization — replace performance-critical Python modules with C++

## 3. Compatibility & Testing
- [ ] Incremental Success — track compatibility percentage (run `test/regression/run_and_report.py` with `PROTOPY_BIN=<build>/src/runtime/protopy`; persist with `--output <path>` or `REGRTEST_RESULTS=<path>`; CTest: `regrtest_protopy_script`)
- [ ] Bug-for-Bug Compatibility — emulate CPython edge cases where safe

## 4. Debugging & IDE Support
- [ ] Protocol Support — DAP (line stepping, variable inspection, breakpoints)
- [ ] IDE Integration — VS Code (debugpy-like), PyCharm
- [ ] Step-through C++ Boundaries — debug across Python/C++ boundary

## 5. Compiler & Deployment (protopyc)
- [ ] AOT Compilation — .py to .so/.dll via C++
- [ ] Static Analysis — type hints for optimization
- [ ] Self-Hosting — protopyc compiles itself

## 6. Installation & Distribution
- [ ] Packaging — pip and wheels
- [ ] Virtual Environments — full venv support
- [ ] Drop-in replacement — protopy aliased to python

---
*Updated from plan implementation. Mark items complete as work progresses.*
