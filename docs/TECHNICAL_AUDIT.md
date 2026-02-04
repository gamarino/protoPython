# protoPython: Comprehensive Technical Audit

**Document version:** 1.0  
**Audit date:** February 2026  
**Scope:** protoPython repository (runtime library, protopy CLI, tests, documentation, build system).

---

## 1. Executive Summary

protoPython is a GIL-less, Python 3.14–compatible runtime built on **protoCore**. It provides a shared library (`libprotoPython.so`), a minimal CLI runtime (`protopy`), and a standard library overlay. The project has completed Phases 1–4 of its implementation plan (foundation, object model, execution engine, and GIL-less concurrency audit). Architecture is coherent, documentation is extensive, and the build and test harness are in place. The main technical risks are the single-context allocation model for true multi-threaded execution, the dependency on a sibling protoCore tree, and the large surface of stub modules and deferred builtins. This audit summarizes the architecture, build and test setup, concurrency model, code quality, documentation, security and robustness considerations, and recommended next steps.

---

## 2. Project Overview

### 2.1 Goals and Scope

- **Primary goal:** Provide a high-performance, GIL-less Python 3.14–compatible environment by mapping Python semantics onto protoCore’s object model and concurrency primitives.
- **In scope (current):** PythonEnvironment, object/type/int/str/list/dict/tuple/set/bytes, native builtins/sys/io/collections/exceptions, tokenizer/parser/compiler, bytecode execution engine (opcodes 100–155), module resolution (Python + native providers), protopy CLI, regression harness.
- **Out of scope or deferred:** protopyc AOT compiler (CMake subdirectory present but not built), HPy extension support, full CPython regrtest parity, real threading module implementation.

### 2.2 Components

| Component | Location | Role |
|-----------|----------|------|
| **protoPython library** | `src/library/`, `include/protoPython/` | Core runtime: PythonEnvironment, ExecutionEngine, Compiler, Parser, Tokenizer, BuiltinsModule, SysModule, IOModule, CollectionsModule, ExceptionsModule, etc. |
| **protopy** | `src/runtime/main.cpp` | CLI: argument parsing, PythonEnvironment creation, module/script resolution, executeModule/runModuleMain. |
| **Standard library** | `lib/python3.14/` | Pure-Python stdlib overlay (CPython 3.14–style); many modules are stubs or partial. |
| **Tests** | `test/library/`, `test/regression/` | Unit tests (test_foundation, test_execution_engine, test_minimal, test_protocore_minimal), regression runner, persistence, CLI tests. |

---

## 3. Architecture

### 3.1 Dependency on protoCore

- protoPython **requires** protoCore as a sibling directory (`../protoCore`). The root `CMakeLists.txt` does `add_subdirectory(../protoCore ...)` and the library links `protoCore` (PUBLIC).
- **Implications:** No standalone build; version coupling is implicit (no version pin). Build and install procedures assume a single parent workspace (see [INSTALLATION.md](INSTALLATION.md)).

### 3.2 Core Abstractions

- **PythonEnvironment:** Owns a `ProtoSpace`, a single root `ProtoContext`, and prototypes (object, type, int, str, list, dict, tuple, set, bytes, slice). It registers the context in a static map for `fromContext()` lookups, initializes sys/builtins and root objects, and provides `resolve()`, `executeModule()`, `runModuleMain()`, trace/pending-exception API, and resolve cache. All of this is the central entry point for the runtime.
- **Execution pipeline:** Source → Tokenizer → Parser (AST) → Compiler (bytecode) → code object → `runCodeObject(ctx, codeObj, frame)`. Frames are `ProtoObject` instances; execution uses the same `ProtoContext` for allocation and GC.
- **Module resolution:** protoCore’s `getImportModule` (SharedModuleCache, moduleRoots) is used. PythonModuleProvider and NativeModuleProvider supply Python and C++ modules; resolution is documented as thread-safe in [GIL_FREE_AUDIT.md](GIL_FREE_AUDIT.md).

### 3.3 Data and Object Model

- Python types map to protoCore types: list → ProtoList (e.g. `__data__` attribute), dict → ProtoSparseList, set → ProtoSet, etc. Mutations use immutable-style APIs (e.g. `appendLast`/`setAt` return new structures) and then `setAttribute` (lock-free CAS on `mutableRoot` per protoCore).
- Exceptions use an `exceptions` module and `PythonEnvironment::setPendingException` / `takePendingException`; container operations (e.g. KeyError on dict) set pending exception for the caller to observe.

---

## 4. Build System and Dependencies

- **CMake:** Minimum 3.20; C++20 required. Single top-level project with subdirectories for library, runtime, tests, and regression.
- **Build products:** Shared library `protoPython`, executable `protopy`; tests: `test_foundation`, `test_execution_engine`, `test_minimal`, `test_protocore_minimal`, `test_regr`; CTest entries for regrtest script, persistence, CLI (help, missing module, script success, bytecode-only). Optional benchmarks via `PROTOPY_BENCHMARKS`.
- **Install:** Components `protoPython` (binaries + library), stdlib `lib/python3.14` to `CMAKE_INSTALL_DATAROOTDIR/protoPython`. CPack supports DEB and RPM.
- **Dependencies:** protoCore (sibling), C++20 compiler; optional Python3 for regression persistence test.

---

## 5. Source Organization and Code Quality

- **Layout:** Public API under `include/protoPython/` (one header per major module); implementation in `src/library/` (flat list of .cpp files). Naming is consistent (e.g. `PythonEnvironment.h/cpp`, `ExecutionEngine.cpp`).
- **No TODO/FIXME/XXX/HACK** markers found in `src/` in the audited snapshot; codebase appears maintained.
- **Thread safety:** Trace function, pending exception, context-registration map, and resolve cache are protected by mutexes (Phase 4). sys.path/sys.modules and deque policy are documented; deque is explicitly not thread-safe for concurrent mutation of the same instance.

---

## 6. Concurrency and Thread Safety

- **Policy:** Single PythonEnvironment and single ProtoContext may be used from multiple threads. Attribute mutations rely on protoCore’s lock-free CAS; protoPython’s shared state is made thread-safe (see [GIL_FREE_AUDIT.md](GIL_FREE_AUDIT.md)).
- **Allocation/GC constraint:** ProtoSpace, ProtoContext, and ProtoThread are tied: allocation and GC are per-thread. Using one ProtoContext from multiple OS threads without per-thread ProtoThread registration would risk allocator/GC corruption. The Phase 4 concurrent test therefore **serializes** compile+run (mutex around the sections that allocate and execute) while still running two threads that call into the same env to stress context map, resolve cache, and trace.
- **Recommendation:** True concurrent execution from multiple OS threads would require either per-thread ProtoThread/context registration (if supported by protoCore) or an explicit execution model (e.g. work dispatched to a single executor thread).

---

## 7. Testing and Quality Assurance

- **test_foundation:** Broad unit tests for PythonEnvironment, types, builtins, sys, io, resolve, executeModule, and many math/builtin/type behaviors. Large number of test cases; filtered CTest (`test_foundation_filtered`) exists to avoid long-running or flaky cases (e.g. GC-related).
- **test_execution_engine:** Opcode-level tests for the bytecode executor (LOAD_CONST, STORE_NAME, BINARY_*, CALL_FUNCTION, LOAD_ATTR, STORE_ATTR, BUILD_*, etc.), including hand-built bytecode and compiled snippets. Includes **ConcurrentExecutionTwoThreads** (serialized execution, two threads, single env).
- **test_minimal / test_protocore_minimal:** Minimal reproducers (with and without protoPython) for startup/GC behavior.
- **test_regr:** Regression harness (module resolution, etc.).
- **CTest:** regrtest script, persistence, CLI help/missing-module/script/bytecode-only. Timeouts set (e.g. 60s for test_foundation, 30s for filtered).
- **Coverage:** Execution engine opcodes 100–155 are implemented and covered by dedicated or combined tests; matrix in [EXECUTION_ENGINE_OPCODES.md](EXECUTION_ENGINE_OPCODES.md). Some opcodes lack a dedicated test (e.g. FOR_ITER full loop).

---

## 8. Documentation

- **Design and planning:** [DESIGN.md](../DESIGN.md), [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md), [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md).
- **Concurrency and audit:** [GIL_FREE_AUDIT.md](GIL_FREE_AUDIT.md) (policy, per-component table, Phase 4 completion).
- **Feature and compatibility:** [EXCEPTIONS.md](EXCEPTIONS.md), [STRING_SUPPORT.md](STRING_SUPPORT.md), [SET_SUPPORT.md](SET_SUPPORT.md), [EXECUTION_ENGINE_OPCODES.md](EXECUTION_ENGINE_OPCODES.md), [STUBS.md](STUBS.md), [FULL_STUB_IMPLEMENTATION.md](FULL_STUB_IMPLEMENTATION.md), [C_MODULES_TO_REPLACE.md](C_MODULES_TO_REPLACE.md).
- **Operations:** [INSTALLATION.md](INSTALLATION.md), [TESTING.md](TESTING.md), [STARTUP_GC_ANALYSIS.md](STARTUP_GC_ANALYSIS.md), [PERFORMANCE_HANG_DIAGNOSIS.md](PERFORMANCE_HANG_DIAGNOSIS.md).
- **Incremental delivery:** Multiple “Next 20 Steps” documents (v4–v35) record completed batches of builtins, opcodes, stubs, and tests. This provides a clear audit trail and scope control.

---

## 9. Security and Robustness

- **Input handling:** CLI parses `--module`, `--script`, `--path`, `--stdlib`; script path is used for file existence and module name derivation. No deep security review of parser/compiler or file I/O was performed; assume untrusted input requires further hardening.
- **Exit codes:** Documented and used (0 success, 64 usage, 65 resolve, 70 runtime), which aids scripting and CI.
- **Resource limits:** No explicit documentation of memory or CPU limits; GC and allocation are delegated to protoCore.
- **Stub behavior:** Many builtins and stdlib modules are stubs (return None, empty dict, or no-op). Callers may not always check for stub behavior; compatibility and safety depend on documented stub contracts and gradual replacement.

---

## 10. Gaps, Risks, and Recommendations

### 10.1 Gaps

- **protopyc:** Referenced in DESIGN and README but not built (`add_subdirectory(src/compiler)` commented out). No AOT compilation path in current tree.
- **HPy:** Planned in IMPLEMENTATION_PLAN but not started.
- **Threading:** Python `threading` is stubbed; no real OS-thread or ProtoThread-based concurrency for user code.
- **Per-thread execution:** Single ProtoContext implies serialized allocation when multiple OS threads share it; true multi-threaded execution would need a defined model (per-thread contexts or single executor).
- **Stubs and compatibility:** Large number of stub modules and deferred builtins (help, input, breakpoint, globals, locals, memoryview, etc.); regrtest parity is incremental.

### 10.2 Risks

- **protoCore coupling:** Version and ABI are not pinned; breaking changes in protoCore can break protoPython. Recommend documenting a supported protoCore version or commit range and CI against it.
- **GC and startup:** Historical GC deadlock was fixed in protoCore (see STARTUP_GC_ANALYSIS.md); intermittent benchmark timeouts are documented (PERFORMANCE_HANG_DIAGNOSIS.md). Remains an area to monitor.
- **Single-context concurrency:** Current design allows multi-threaded *use* of the same env/context only with serialized execution in the test; any future “true” concurrent execution must respect ProtoSpace/ProtoContext/ProtoThread allocation rules.

### 10.3 Recommendations

1. **Document protoCore version:** In INSTALLATION or a VERSION/COMPATIBILITY file, state the recommended or tested protoCore version/commit.
2. **CI matrix:** Run tests (at least test_foundation_filtered, test_execution_engine, test_regr, CLI tests) in CI; optionally run with AddressSanitizer/ThreadSanitizer.
3. **Concurrency doc:** Add a short “Concurrent execution model” section to DESIGN or GIL_FREE_AUDIT that states: (a) shared env/context is safe for thread-safe layers (trace, resolve, context map); (b) allocation/GC require per-thread ProtoThread or serialized execution; (c) current test serializes compile+run by design.
4. **Stub inventory:** Keep STUBS.md and FULL_STUB_IMPLEMENTATION.md updated as stubs are replaced or behavior changes.
5. **Regression tracking:** Use existing `run_and_report.py` and persistence to track regrtest compatibility over time and surface regressions early.

---

## 11. Conclusion

protoPython is a well-structured GIL-less Python 3.14 runtime with a clear separation between environment, execution engine, and CLI. Phases 1–4 are complete; documentation and concurrency audit are in place; build, test, and install are functional. The main technical constraints are the dependency on protoCore’s allocation/GC model (single context vs. multiple threads), the number of stub modules, and the deferred protopyc/HPy work. Addressing the recommended items (protoCore versioning, CI, concurrency summary, stub and regression tracking) will strengthen maintainability and set a solid base for Phase 5+ (debugging/tooling, protopyc, full compatibility).

---

*This audit was produced for the protoPython repository. For design and implementation details, see DESIGN.md, IMPLEMENTATION_PLAN.md, and the docs/ directory.*
