# protopyc Compiler

`protopyc` translates Python source into C++ source that calls the protoPython runtime
(`PythonEnvironment`) and protoCore directly. The generated code can be compiled into a
shared library that `protopy` loads as a module. The implementation is in
`src/compiler/` (`ProtopycMain.cpp` for the command line, `CppGenerator.cpp` for code
generation).

## 1. Command line

```text
protopyc <source_path> [--emit-cpp] [--emit-make] [--build-so]
```

The source path must be the first argument. Without a mode option, `--emit-cpp` is
assumed.

| Option | Effect |
|--------|--------|
| `--emit-cpp` | Generate C++ source files only. |
| `--emit-make` | Also write a `Makefile`. |
| `--build-so` | Write the `Makefile` and run `make` to build the shared library. |

- If `<source_path>` is a file, `protopyc` writes `<name>.cpp`, and the `Makefile` when
  requested, to the current directory.
- If `<source_path>` is a directory, `protopyc` processes every `.py` file below it and
  mirrors the directory tree under `./out/`, placing each `.cpp` file at the
  corresponding relative position. The `Makefile` is written to `./out/`, and
  `--build-so` runs `make` there.

The generated `Makefile` compiles with `g++ -O3 -fPIC -std=c++20`, links
`-lprotoPython -lprotoCore` and names its target `module.so`. Its include and library
directories (`INCLUDES`, `LDFLAGS`) are fixed paths from the original development
layout rather than values derived from the build, so they must be edited before the
`Makefile` is used on another machine.

Parse errors are reported as `file:line:column: message`, and `protopyc` stops at the
first one.

Exit status: 0 on success; 1 when no arguments are given (the usage text is printed),
the source path does not exist, an option is unknown, parsing or code generation fails,
or `make` fails.

## 2. Loading generated modules

Every generated file defines `extern "C" void* proto_module_init()`, which obtains the
current context with `protoPython::PythonEnvironment::getCurrentContext()` and runs the
module body.

`CompiledModuleProvider` (`src/library/CompiledModuleProvider.cpp`) loads these
libraries. It is one of the module providers that `PythonEnvironment` registers with
protoCore's provider registry (Unified Module Discovery, UMD). For a module name it
replaces dots with `/`, looks for `<name>.so` in the search directories, opens the
library with `dlopen(RTLD_NOW | RTLD_GLOBAL)`, looks up `proto_module_init`, creates
the module object (`__name__`, `__file__`, `__loader__`) and calls the initializer with
that module as its globals.

The `Makefile` always names its target `module.so`, so rename the library to
`<module>.so` (for example `foo.so` for `import foo`) before placing it on the search
path.

`from ... import` statements in generated code call
`PythonEnvironment::importModule(name, level, names)` at run time; relative imports are
resolved then, not at compile time.

## 3. Debugging

The generated C++ contains `#line` directives that refer to the original `.py` file, so
compiler diagnostics and native debuggers (GDB, LLDB) can point at Python source lines.

## 4. Generated code

- **Values** are `const proto::ProtoObject*`; `None`, `True` and `False` are the
  protoCore constants `PROTO_NONE`, `PROTO_TRUE` and `PROTO_FALSE`.
- **Runtime helpers**: operators, subscripts, attributes and calls go through
  `PythonEnvironment` members such as `binaryOp`, `getItem`/`setItem`,
  `getAttr`/`setAttr`, `callObject`/`callObjectEx`, `lookupName`/`storeName`,
  `iter`/`next` and `importModule`.
- **Small-integer fast path**: arithmetic and comparisons are computed inline when both
  operands are protoCore small integers, and fall back to `env->binaryOp` otherwise;
  `is` and `is not` compile to pointer comparisons.
- **Collections**:
  - list literals build a `ProtoList` with `appendLast` and store it in the `__data__`
    attribute of an object whose parent is the `list` prototype;
  - dict literals store values in a `ProtoSparseList` keyed by hash (`__data__`) and the
    keys in insertion order in a `ProtoList` (`__keys__`), on an object whose parent is
    the `dict` prototype;
  - tuple literals are built as a list and converted with `ctx->newTupleFromList`.

## 5. Not implemented

An earlier version of this specification described features that `protopyc` does not
have:

- a C++ abstraction layer of `py::` types (such as `py::list` or `py::actor`) and an
  actor-based runtime; generated code uses `PythonEnvironment` and protoCore calls;
- compile-time resolution of relative imports;
- static type inference beyond the small-integer fast path;
- C++ namespaces that mirror the Python module hierarchy.
