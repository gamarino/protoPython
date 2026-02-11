# Technical Specification: protopyc Compiler Infrastructure

## 1. Overview
The `protopyc` utility is the primary build tool for the protoPython ecosystem. Its mission is to transform Python source code into high-performance, C++-native binaries that adhere to the protoCore UMD (Universal Module Definition) standard, ensuring full compatibility with the underlying actor-based runtime and memory model.

## 2. Command Line Interface (CLI) & Workflow
**Usage:** `protopyc <source_path> [options]`

### Source Mapping:
- If `<source_path>` is a file, `protopyc` generates a single `.cpp` file.
- If `<source_path>` is a directory (Python package), `protopyc` must perform an isomorphic transformation, replicating the entire directory tree in the output folder, placing each generated `.cpp` file in its corresponding relative position.

### Output Modes:
- `--emit-cpp`: Generates only the C++ source files and headers.
- `--emit-make`: Generates C++ files plus a specialized Makefile (or build script) pre-configured with protoCore and protoPython dependencies.
- `--build-so`: Orchestrates the full compilation pipeline to produce a Universal Module (`.so` shared library).

## 3. Module Resolution & Relative Imports
The compiler must implement a robust Import Resolution Engine that mirrors Python’s PEP 328 logic but bridges it to C++ UMD loading:

- **Relative Import Translation:** `from . import submodule` or `from ..parent import sibling` must be resolved at compile-time to identify the target UMD symbols.
- **UMD Loading Integration:** Instead of standard Python import hooks, the generated code must call the protoPython Module Loader services. These calls must be resolved via the UMD entry point of the target shared library.
- **Namespace Consistency:** The compiler must ensure that the internal C++ namespace structure of the generated code reflects the Python module hierarchy to avoid symbol collisions during dynamic linking.

## 4. Compilation Constraints & Environment
- **Prerequisites:** The build environment must have `protoCore.h`, `protoPython.h`, and their respective shared libraries (`libprotocore.so`, `libprotopython.so`) installed and accessible via standard system paths or environment variables.
- **Standardized Entry Point:** Every generated module must export a unique, standardized UMD symbol (e.g., `extern "C" void* proto_module_init()`). This symbol provides the runtime with the module's metadata, type information, and actor-safe dispatch tables.

## 5. Developer Experience (DX) & Debugging
- **Fail-Fast Error Handling:** The compiler must terminate execution upon the first encountered error (syntax, type-inference, or resolution error). Error messages must follow the standard format: `filename:line:column: error_message` for seamless IDE integration.
- **Source Mapping via #line Directives:** To facilitate debugging, the generated C++ code must include `#line` directives pointing back to the original `.py` source. This enables:
    - Native debuggers (GDB/LLDB) to step through Python code while executing the C++ binary.
    - Runtime stack traces to report errors in the context of the original Python logic.
- **Code Legibility:** The output C++ must be clean and human-readable, utilizing protoPython's native abstractions (e.g., `py::list`, `py::actor`). This allows the generated code to serve as a reference or a base for manually optimized native extensions.

- **Static Type Inference:** Where possible, `protopyc` should infer types to bypass the overhead of dynamic dispatch, mapping Python variables to native protoCore cell types.

## 7. Implementation Details: Runtime Support & Patterns

### 7.1 PythonEnvironment Helper Layer
To keep the generated C++ code concise and handle Python's dynamic nature, `protopyc` relies on a set of helper methods in the `protoPython::PythonEnvironment` class:
- **`binaryOp(a, op, b)`**: Centralizes operator dispatch, including support for Python's dunder methods (`__add__`, `__mul__`, etc.) and basic type optimization for integers and floats.
- **`getItem(container, key)` / `setItem(container, key, value)`**: Abstracts collection access, handling `ProtoList`, `ProtoSparseList`, and user-defined `__getitem__`/`__setitem__`.
- **`getAttr(obj, name)` / `setAttr(obj, name, value)`**: Standardizes attribute access via `ProtoString` mapping.
- **`callObject(callable, args)`**: Handles the complexity of invoking Python objects with varying argument lists.

### 7.2 Native Collection Mapping
Python collection literals are translated into efficient `protoCore` incremental builders:
- **Lists**: Created via `ctx->newList()` and populated using `appendLast()`.
- **Dictionaries**: Implemented using `ctx->newSparseList()` for data storage and `ctx->newList()` for key tracking, ensuring $O(1)$ average access for small keys while maintaining Python's insertion order and `__data__`/`__keys__` attribute structure.
- **Tuples**: Built as lists and finalized using `ctx->newTupleFromList()`.

### 7.3 API Standardization
Generated code must strictly adhere to the `protoCore` and `protoPython` memory model:
- **Const Correctness**: Most objects are passed as `const proto::ProtoObject*`.
- **Context Access**: The execution context is retrieved via `protoPython::PythonEnvironment::getCurrentContext()` during module initialization.
- **Macro Usage**: Standard Python values use global macros: `PROTO_NONE`, `PROTO_TRUE`, `PROTO_FALSE`.
