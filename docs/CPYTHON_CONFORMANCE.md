# CPython Conformance Tracker

This document tracks the progress of `protoPython` in passing the official CPython Regression Test Suite (`Lib/test`). Achieving "All Green" in the Essential category is the primary goal for industrial-grade stability.

## Test Priorities

### 🔴 Essential (Primary Language & Core Types)
Core syntax, standard object model, and fundamental types.

- [/] `test_grammar.py`: Validates parser completeness. (Compiler fixed, runtime modules pending)
- [ ] `test_types.py`: Fundamental behavior of core object cells.
- [ ] `test_descr.py`: MRO, descriptors, and slots.
- [ ] `test_generators.py`: Generator execution state.
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

## Progress Summary

| Category | Total | Checked | Passed | Success Rate |
| :--- | :--- | :--- | :--- | :--- |
| **Essential** | 7 | 1 | 0.5 | 7% |
| **Important** | 6 | 0 | 0 | 0% |
| **Necessary** | 5 | 0 | 0 | 0% |
| **Low Priority**| 4 | 0 | 0 | 0% |
| **Total** | **22** | **1** | **0.5** | **2%** |

## Recent Achievements (V70-V74)

- **Compiler Conformance**: 
    - Full support for `//`, `@`, `**` operators and augmented assignments.
    - Implemented Walrus operator (`:=`) support.
    - Robust `async for` and `async with` compilation with `else` block support.
    - Improved recursive `del` target handling.
    - Support for empty `suite` blocks.
- **Execution Engine**:
    - Implemented `OP_DICT_UPDATE`, `OP_LIST_EXTEND`, `OP_SET_UPDATE` for efficient collection operations.
    - Added `OP_RERAISE` and `OP_POP_EXCEPT` for correct exception handling flow.
    - Matrix multiplication (`@`) and floor division (`//`) support.

## Benchmarking with PyPerformance
Progress in running the `PyPerformance` suite is tracked separately in the [Performance Analysis](file:///home/gamarino/Documentos/proyectos/protoPython/docs/PERFORMANCE_ANALYSIS.md) (if exists).
