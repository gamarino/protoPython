# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.3] - 2026-02-23

### Fixed

- **Bindings**: Renamed native functools module to `_functools` to allow standard library script loading.
- **Engine**: Fixed `PROTO_NONE` handling during dictionary lookups in `executeBytecodeRange`.
- **Compiler**: Verified `co_consts` and `co_code` as tuples (not lists) during code object execution.
- **Tests**: Introduced `invokeCallable` helper in C++ tests to properly invoke the `__call__` method when testing function objects.
- **Threading**: Exposed `RLock` alias mapping to `allocate_rlock`.
- **Build**: Added `-fno-delete-null-pointer-checks` compilation flag.

## [0.2.2] - 2026-02-18

### Fixed

- **Attribute Lookup**: Resolved `AttributeError: 'type' object has no attribute 'add'` by correcting internal method mapping for `set.add` (mapped to `add` instead of `__add__`).
- **Object Identity**: Standardized `__class__` attribute assignment across all core builtin constructors (`set`, `list`, `dict`, `tuple`, `bytes`, `object`). This ensures instances correctly identify as their respective types rather than inheriting `type` from the prototype.
- **Compiler Reliability**: Standardized the `makeCodeObject` internal API across `Compiler.cpp` and `PythonEnvironment.cpp` to ensure consistent metadata (line numbers, flags) for all generated code objects.
- **Diagnostics**: Cleaned up internal debug prints and enabled smoother `abc` module imports by resolving `GenericAlias` interaction bugs.
- **Exceptions**: Fixed `py_tuple_call` to correctly propagate non-StopIteration exceptions (e.g., `TypeError`) instead of suppressing them, which was masking critical errors.
- **Attribute Lookup**: Fixed a critical recursion bug in `PythonEnvironment::getAttribute` where `getAttrDepth` was not decremented on failed lookups, leading to false "recursion limit reached" errors and unbound method failures in `namedtuple` construction.

## [0.2.1] - 2026-02-17

### Fixed

- **Garbage Collection (GC) Safety**: Massive refactoring of the `ExecutionEngine` to ensure all `ProtoObject*` operands and intermediate results are rooted on the execution stack during bytecode execution. This prevents premature object reclamation during complex operations.
- **Opcode Stability**: Refactored major opcode families for GC safety:
  - Arithmetic and Bitwise (Binary and In-place).
  - Container mutations (`LIST_APPEND`, `SET_ADD`, `MAP_ADD`, `DICT_UPDATE`, `LIST_EXTEND`).
  - Attribute and Subscript access (`LOAD_ATTR`, `STORE_ATTR`, `BINARY_SUBSCR`, `STORE_SUBSCR`, `DELETE_SUBSCR`).
  - Iterator and Generator control flow (`GET_ITER`, `YIELD_FROM`, etc.).
  - Function calls (`CALL_FUNCTION`, `CALL_FUNCTION_KW`, `CALL_FUNCTION_EX`).
- **Critical Bug**: Fixed a stack indexing bug in `OP_MAP_ADD` that could lead to stack corruption or incorrect attribute mapping.
- **Stability**: Resolved `std::length_error` crashes caused by unsanitized stack manipulations and missing GC roots.
- **Python Parity**: Aligned `STORE_ATTR` and `STORE_SUBSCR` stack order with CPython 3.14 standards.
- **Test Alignment**: Updated unit tests in `TestExecutionEngine.cpp` to match Python-compliant stack rotation and attribute storage patterns.

### Changed

- **Performance Optimization**: Implemented internal dunder string caching in `PythonEnvironment` using `getInternalString` to accelerate attribute and method lookups.
- Improved stack management strategy to favor in-place modification over frequent pop/push cycles, enhancing performance and safety.

## [0.2.0] - 2026-02-14

### Added

- **Professional Documentation**: Comprehensive User Guide, C++ API Reference, Internals Deep Dive, and Python Compatibility Guide.
- **Example Suite**: New examples for generators, async/await, multithreading, and C++ embedding.
- **Improved Test Coverage**: Added dedicated tests for native generators, async coroutines, and parallel threading.
- **Generator Metadata**: Added `co_name` to code objects for better introspection and naming in tracebacks.
- **Enhanced Types**: Standardized `__repr__` and `__str__` lookups in `PythonEnvironment`.

### Fixed

- **Execution Engine Regressions**: Resolved critical instruction stepping issues that caused bytecode misalignment.
- **Stack Integrity**: Implemented `GCStack` fallback for unit tests to prevent stack overflows.
- **Opcode Logic**: Corrected stack order in `STORE_ATTR` and fixed `GET_ITER` return behavior to match expected norms.
- **Build and Syntax**: Fixed various compilation errors and missing closing braces in `ExecutionEngine.cpp`.

### Changed

- Improved `type(None)` to correctly return `NoneType`.
- Optimized dunder lookups for both built-in and user-defined types.

## [0.1.0] - 2026-02-10

### Added

- Initial release of protoPython.
- Basic bytecode interpreter for Python 3.14.
- GIL-free concurrency model based on protoCore.
- Support for core built-in types and functions.
- C++ interop bridge using HPy.
