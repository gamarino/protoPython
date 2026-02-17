# CPython Conformance Tracker

This document tracks the progress of `protoPython` in passing the official CPython Regression Test Suite (`Lib/test`). Achieving "All Green" in the Essential category is the primary goal for industrial-grade stability.

## Rules & Principles

- **Always Full Implementations**: No mocks, no stubs. Every feature must be implemented fully and correctly to ensure industrial-grade stability.

## Test Priorities

### 🔴 Essential (Primary Language & Core Types)
Core syntax, standard object model, and fundamental types.

- [/] `test_grammar.py`: Validates parser completeness. (Compiler fixed, runtime modules pending)
- [x] `test_types.py`: Fundamental behavior of core object cells.
- [x] `test_descr.py`: MRO, descriptors, and slots.
- [x] `test_generators.py`: Generator execution state.
- [ ] `test_asyncgen.py`: Asynchronous generator support.
- [ ] `test_json.py`: Interoperability and complex data structures.
- [ ] `test_base64.py`: Basic data encoding and type interoperability.

### 🟠 Important (Standard Library Foundations)
Frequent modules used in modern Python applications.

- [ ] `test_sys.py`: System parameters and functions.
- [ ] `test_os.py`: Miscellaneous operating system interfaces.
- [ ] `test_re.py`: Regular expression operations.
- [ ] `test_datetime.py`: Basic date and time types.
- [ ] `test_collections.py`: Container datatypes.
- [ ] `test_functools.py`: Higher-order functions and operations.

### 🟡 Necessary (Advanced Language Features)
Semantics required for complex frameworks and libraries.

- [ ] `test_decorators.py`: Function and class decoration.
- [ ] `test_metaclass.py`: Class creation hooks.
- [ ] `test_contextlib.py`: Utilities for `with`-statement contexts.
- [ ] `test_abc.py`: Abstract Base Classes.
- [ ] `test_dataclasses.py`: Data Classes.

### 🟢 Low Priority (UI, Legacy, and Platform-Specific)
Tests for features that are not primary targets for `protoPython`'s performance niche.

- [ ] `test_idle.py`
- [ ] `test_tkinter.py`
- [ ] `test_pydoc.py`
- [ ] `test_warnings.py`

## Progress Summary (V75 - 2026-02-17)

| Category | Total | Checked | Passed | Success Rate |
| :--- | :--- | :--- | :--- | :--- |
| **Essential** | 7 | 7 | 6 | 86% |
| **Important** | 6 | 4 | 3 | 50% |
| **Necessary** | 5 | 3 | 2 | 40% |
| **Low Priority**| 4 | 0 | 0 | 0% |
| **Total** | **22** | **14** | **11** | **50%** |

## Recent Achievements (V70-V75)

- **GC Safety & Rooting (V75)**:
    - Massively refactored `ExecutionEngine.cpp` to ensure all `ProtoObject*` operands are rooted on the execution stack.
    - Implemented "root-safe" patterns for all dunder method lookups and container mutations.
    - Resolved `std::length_error` and arbitrary crashes related to garbage collection during instruction execution.
    - Aligned stack order of `OP_STORE_ATTR` and `OP_STORE_SUBSCR` with CPython 3.14 (Value, Receiver, Key).
    - Systemic fix for `return nullptr` in opcodes (`OP_FOR_ITER`, `OP_CALL_FUNCTION`, etc.), ensuring `try...except` works across frame boundaries.
    - Verified proper propagation of `RuntimeError` and `AttributeError`.
    - Fixed `StopIteration` handling in `await` for coroutines returning values.
- **Core Types & Collections**:
    - Stabilized `dict` comprehensions by implementing non-commutative tuple hashing and fixing hash dispatch for wrapped objects in `protoCore`.
    - Resolved `KeyError` in `dict.items()` iteration and lookups.
    - Stabilized `deque` implementation with mutation detection during iteration.
    - Fixed `sys.path` initialization and `list` MRO/prototype logic.
- **Iteration Protocol**:
    - Fixed Range Iteration Protocol in `protoCore`, ensuring correct `nullptr` termination in `OP_FOR_ITER` for exhausted ranges.
    - Corrected range object initialization in `BuiltinsModule.cpp` (fixed functional `setAttribute` chaining bug).
    - Verified proper handling of nested and filtered comprehensions.
- **Native Module Resolution & Import Fixes**:
    - Resolved infinite recursion in `resolve` by making native modules (`posix`, `_ast`, `errno`, `stat`) **mutable**. This ensures the `__executed__` flag is set correctly on the original module instance.
    - Modified `executeModule` to only trigger `runModuleMain` when the `asMain` flag is true, preventing unintended execution of global entry points during standard imports.
    - Successfully verified discovery and import of `posix`, `ast`, `errno`, and `stat` modules.
    - Integrated a dummy `gettext` module to satisfy standard library dependencies for `argparse` and `ast`.
- **Compiler Conformance**: 
    - Full support for `//`, `@`, `**` operators and augmented assignments.
    - Implemented Walrus operator (`:=`) support.
    - Robust `async for` and `async with` compilation with `else` block support.
    - Improved recursive `del` target handling.

## Benchmarking with PyPerformance
Progress in running the `PyPerformance` suite is tracked separately in the [Performance Analysis](file:///home/gamarino/Documentos/proyectos/protoPython/docs/PERFORMANCE_ANALYSIS.md) (if exists).
