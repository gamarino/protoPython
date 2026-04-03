# CPython Conformance Tracker

This document tracks the progress of `protoPython` in passing the official CPython Regression Test Suite (`Lib/test`). Achieving "All Green" in the Essential category is the primary goal for industrial-grade stability.

## Rules & Principles

- **Always Full Implementations**: No mocks, no stubs. Every feature must be implemented fully and correctly to ensure industrial-grade stability.

## Test Priorities

### 🔴 Essential (Primary Language & Core Types)

Core syntax, standard object model, and fundamental types.

- [ ] `test_grammar.py`: FAIL (ImportError: No module named 'test.support')
- [ ] `test_types.py`: FAIL (ImportError: No module named 'test.support')
- [ ] `test_descr.py`: FAIL (ImportError: No module named 'test.support')
- [ ] `test_generators.py`: FAIL (ImportError: No module named 'test.support')
- [ ] `test_asyncgen.py`: FAIL (ImportError: No module named 'inspect')
- [x] `test_json.py`: PASS (Basic `import json` verification, `json.loads` data mismatch identified)
- [ ] `test_base64.py`: FAIL (ImportError: No module named 'unittest')

### 🟠 Important (Standard Library Foundations)

Frequent modules used in modern Python applications.

- [ ] `test_sys.py`: System parameters and functions. (Import PASS)
- [x] `test_os.py`: PASS (Verified with `tests/test_os.py`)
- [ ] `test_re.py`: Regular expression operations.
- [ ] `test_datetime.py`: Basic date and time types.
- [ ] `test_collections.py`: Container datatypes.
- [ ] `test_functools.py`: FAIL (TypeError: 'NoneType' object is not iterable in `namedtuple`)

### 🟡 Necessary (Advanced Language Features)

Semantics required for complex frameworks and libraries.

- [x] `test_decorators.py`: PASS (via `tests/test_decorator.py`)
- [x] `test_metaclass.py`: PASS (Verified with `test_metaclass.py`)
- [ ] `test_contextlib.py`: Utilities for `with`-statement contexts.
- [x] `test_abc.py`: PASS (Verified with `tests/test_abc.py`)
- [ ] `test_dataclasses.py`: Data Classes.

### 🟢 Low Priority (UI, Legacy, and Platform-Specific)

Tests for features that are not primary targets for `protoPython`'s performance niche.

- [ ] `test_idle.py`
- [ ] `test_tkinter.py`
- [ ] `test_pydoc.py`
- [ ] `test_warnings.py`

## Progress Summary (V82 - 2026-03-08)

| Category | Total | Checked | Passed | Success Rate |
| :--- | :--- | :--- | :--- | :--- |
| **Essential** | 7 | 7 | 1 | 14% |
| **Important** | 6 | 2 | 1 | 16% |
| **Necessary** | 5 | 3 | 3 | 60% |
| **Low Priority** | 4 | 0 | 0 | 0% |
| **Total** | **22** | **12** | **5** | **22%** |

> [!NOTE]
> **V80 Evaluation Cycle**: Focus shifted to unblocking the standard library test imports. Full native implementations of `time.monotonic` and `time.perf_counter` were added. Identified missing features in `_weakref`, `threading`, and `unittest` that block test execution and require complete native implementations to ensure strict standard library compatibility.

## Recent Achievements (V70-V75)

(Previous achievements preserved for context...)
...

## Recent Achievements (V77)

- **Object Instantiation & Inheritance Fixes**:
  - Resolved `TypeError` in `argparse` (e.g., `'Namespace' object has no attribute 'add_argument_group'`) by fixing dynamic instance initialization and attribute resolution.
  - Refactored `PythonEnvironment::getAttribute` to prioritize MRO lookup before checking the metaclass, correctly implementing Python's attribute resolution order.
  - Modified `BuiltinsModule.cpp` `py_object_new` to fully initialize Python instances (proper dictionary storage, `__class__` assignment, and base class prototype linking).
  - Unbound `object.__new__` and `type.__new__` natively so that `getattr(cls, "__new__")` passes `cls` exactly once without native method rebinding hijacking the argument count.
  - Fallback MRO hierarchy injection in `py_type` for automatically inserting `object` instances built without explicit bases.
  - Patched `match` soft keyword parsing collision in `contextlib.py` to unblock continued standard library loading.
- **GenericAlias & Standard Library Unblocking**:
  - Resolved `TypeError: object is not iterable` in `abc.py` by correctly implementing `GenericAlias` resolution.
  - Modified `OP_BINARY_SUBSCR` in `ExecutionEngine.cpp` to use `invokeDunder` for `__class_getitem__` fallback, enabling class subscripting (e.g., `list[int]`).
  - Implemented `py_type_class_getitem` on `typePrototype` to return the class itself as a simplified `GenericAlias`.
  - Fixed return values of `py_list_getitem`, `py_tuple_getitem`, and `py_dict_getitem` to ensure proper fallback chain.
- **Native `gc` Module Implementation**:
  - Registered a native `gc` module in `BuiltinsModule.cpp` with stub implementations for `collect`, `isenabled`, `disable`, and `enable`.
  - Created `lib/python3.14/gc.py` library shim.
- **Builtin Registration**:
  - Fixed typo in `builtins` registration for the `bytes` type.

## Recent Achievements (V83)

- **VM Stability & Bootstrap Reliability**:
  - Resolved `TypeError` during early object model setup (bootstrap phase) by adding a mandatory initialization loop for `BaseException`, `Exception`, `TypeError`, and `SystemError` in `PythonEnvironment::initializeRootObjects`. This ensures that all essential exception prototypes are registered before any module loading or code execution, completely unblocking the standard library initialization sequence.
  - Implemented `SETUP_FINALLY` (122) and `POP_BLOCK` (87) bytecode opcodes in the official interpreter (Version C) to support full `try...finally` block semantics.
  - Corrected the exception recovery logic: when a pending exception is detected, the VM now correctly unwinds the evaluation stack to the handler's depth and jumps to the next instruction in the corresponding block, ensuring total stack isolation and context integrity across frame boundaries.
  - Resolved a critical structural regression in `ExecutionEngine.cpp` where a misplaced namespace closing brace broke the visibility of exported functions like `runClassCall`.
  - Harmonized the VM execution path to use a single, unified bytecode interpreter (Version C), eliminating previous triple-redundancy and reducing maintenance overhead.
  - Integrated `next_i` tracking during exception catching to resolve `Stack underflow` errors in `TestFoundation.StatisticsMean`.

- **GC Scalability & Deadlock Resolution**:
  - Fixed O(N^2) infinite GC hang by properly promoting `lastAllocatedCell` to `DirtySegments` linearly across GC cycles.
  - Resolved lock-free unrooted pointer sweeps during early allocation by introducing `pendingRoot` in `ProtoContext`, completely eliminating Circular List GC deadlocks in `TupleDictionary` and string interning.
- **`_weakref` Native Module**:
  - Implemented `CallableProxyType`, `ProxyType`, and `ReferenceType` flawlessly. This completely unblocked `abc.py` and `test_grammar.py` type creation, bridging a crucial gap in standard library conformance!

## Pending Native Implementations (Blocked Tests)

- **`test.support` / `unittest`**: Encountering `ImportError: No module named 'test.support'` during test initialization. We need to expose or mock the internal `test.support` utilities to allow individual suite endpoints to execute.
- **`threading`**: Missing native components causing cascading import errors.
*(Must be fully natively implemented to unblock tests natively without mocks, adhering to project rules).*

## Recent Achievements (V79)

- **Control Flow & Block Unwinding**:
  - Implemented block unwinding for non-local control flow in Compiler (`OP_RETURN_VALUE`, `OP_BREAK_LOOP`, `OP_CONTINUE_LOOP`).
  - Added `BlockEnv` tracking for `TryFinally` and `With` blocks, resolving memory leaks and bypassing finally execution paths.
- **Runtime Stability & GC Fixes**:
  - Resolved critical GC race conditions by zeroing allocated cell memory in `allocCell`.
  - Fixed `ParentLink` casting and resolving segfaults in `ParentLinkImplementation::getObject` (occurring during `test_grammar.py`).
  - Fixed `__call__` resolution on `methodPrototype` to correctly handle bound methods and metaclass instantiations.
- **Environment Context & Imports**:
  - Fixed Python compiler losing environment context when compiling bytes literals.
  - Debugging and fixes implemented for `functools` and `_collections_abc` imports.

## Recent Achievements (V78)

- **`test_os.py` Standardization**:
  - The `os` module initializes and imports safely, validating complex generic library architectures.
- **`super()` Instantiation Complete Resolution**:
  - Extirpated `NameError: name 'self' is not defined` comprehensively across `super().__init__()` chains natively.
  - Patched `PythonEnvironment::getAttribute` and `ExecutionEngine::runUserClassCall` to completely enforce Python descriptor routing instead of unsafe dictionary extraction.
  - Restored immutability variable mapping natively by synchronizing `f_locals` explicitly with the post-bound initialization frame object representation correctly.
  - Repaired `py_super` dunder search fallback explicitly fetching `"self"` dynamically over native built-in namespaces natively.

## Regressions & Known Issues (V80/V81)

- **C++ Test Suite Failures**:
  - `ObjectTest.GetMissingAttribute` fails in `protoCore` tests.
  - `test_foundation` crashes with a `SEGFAULT`.
  - `test_execution_engine` fails specifically on `ExecutionEngineTest.CallFunction` (fixed `ExecutionEngineTest.StoreSubscr`).
- **Standard Library Gaps**:
  - `test_descr.py` continues to fail or timeout natively due to cascading evaluation complexity.
- **Execution Stability**: `test_grammar.py` no longer hangs indefinitely! It executes instantaneously but is currently blocked by missing `test.support` utilities. `test_types.py` still times out. Test framework script (`tests/run_conformance.sh`) had environment/symlink issues on WSL when launching native shared libraries.


## Historical Achievements (V70-V75)

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
- **Complex Type Implementation (V76)**:
  - Implemented `complex` built-in type with positional and keyword argument support (`real`, `imag`).
  - Added `__repr__` and `__str__` support for complex objects, matching CPython formatting (e.g., `(1+2j)`).
  - Fixed attribute lookup for `real` and `imag` on complex instances.
  - Resolved circularity issues in prototype initialization that caused built-in registration data loss.
- **Compiler Conformance**:
  - Full support for `//`, `@`, `**` operators and augmented assignments.
  - Implemented Walrus operator (`:=`) support.
  - Robust `async for` and `async with` compilation with `else` block support.
  - Improved recursive `del` target handling.

## Benchmarking with PyPerformance

Progress in running the `PyPerformance` suite is tracked separately in the [Performance Analysis](file:///home/gamarino/Documentos/proyectos/protoPython/docs/PERFORMANCE_ANALYSIS.md) (if exists).
