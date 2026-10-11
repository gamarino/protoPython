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

The generated `Makefile` compiles with `-O3 -fPIC -std=c++20`, links
`-lprotoPython -lprotoCore` and names its target `module.so`. Its compiler, include
directories (`INCLUDES`) and library directories (`LDFLAGS`) are written as follows:

- **Build tree.** When `protopyc` runs from the directory it was built in, the
  directories are those of that build: `include/` in the protoPython source tree,
  protoCore's `headers/` directory, and the directories holding `libprotoPython` and
  `libprotoCore`.
- **Installation.** Any other `protopyc` uses the installation layout, resolved
  relative to its own directory: `<prefix>/include` and `<prefix>/<libdir>`, plus
  protoCore's include and library directories when protoPython was configured with
  `PROTO_CORE_PREFIX`. A relocated prefix therefore keeps working. If protoCore's
  headers or library are elsewhere, set the overrides below.
- **Overrides.** `PROTOPYC_INCLUDE_DIRS` and `PROTOPYC_LIBRARY_DIRS` (`:`-separated)
  replace the include and library directories, and `PROTOPYC_CXX` replaces the
  compiler.
- **Compiler.** `make` uses `CXX` from the environment or its command line when set;
  otherwise the compiler protoPython was built with (or `PROTOPYC_CXX`).
- **Run-time search path.** Every library directory is also passed as
  `-Wl,-rpath,<dir>`, so `module.so` loads without `LD_LIBRARY_PATH`.

`make` cannot handle whitespace in paths, so `protopyc` exits with status 1 when a
directory or the compiler path contains whitespace.

Parse errors are reported as `file:line:column: message`, and `protopyc` stops at the
first one.

Exit status: 0 on success; 1 when no arguments are given (the usage text is printed),
the source path does not exist, an option is unknown, parsing or code generation fails,
a `Makefile` path contains whitespace, or `make` fails.

## 2. Loading generated modules

Every generated file defines `extern "C" void* proto_module_init()`, which obtains the
current context with `protoPython::PythonEnvironment::getCurrentContext()` and runs the
module body in a context of its own, chained to it (section 4, "Garbage collection").

`CompiledModuleProvider` (`src/library/CompiledModuleProvider.cpp`) loads these
libraries. It is one of the module providers that `PythonEnvironment` registers with
protoCore's provider registry (Unified Module Discovery, UMD). For a module name it
replaces dots with `/`, looks for `<name>.so` in the search directories, opens the
library with `dlopen(RTLD_NOW | RTLD_GLOBAL)`, looks up `proto_module_init`, creates
a mutable module object (`__name__`, `__file__`, `__loader__`) and calls the initializer
with that module as the current frame and globals, so the module-level names the
initializer binds become attributes of the imported module.

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
- **Garbage collection.** Generated code keeps every object it still uses where
  protoCore's collector sees it (protoCore `docs/EMBEDDER-CONFORMANCE.md`, rule 3):
  - every compiled callable (function, generator and its continuation, lambda, and the
    module initializer) runs in its own `ProtoContext`, opened by
    `protoPython::ContextScope` and chained to the thread's current context, and its
    result is promoted to the caller's context, as the interpreter does for a call;
  - its locals live in that context's automatic slots: `local_<name>` is a C++
    reference to a slot;
  - the value of every expression node except constants and locals is stored in a
    temporary slot as it is computed, and a `for` loop's iterator, a caught exception,
    a class's bases and namespace, a `with` statement's `__exit__` and the exception
    or return value in transit through a `finally` clause are kept in slots for as
    long as they are used;
  - `ctx->safepoint()` is emitted only at the head of `while` and `for` loops, where
    no temporary of the running statement is live; it clears the temporary slots
    first, so they do not retain what the previous iteration computed. Comprehension
    loops emit no safepoint: their garbage becomes collectable at the enclosing
    loop's next safepoint or when the callable returns.

  Before this (protoPython issue #2), compiled code ran in its caller's context, held
  its locals only in C++ variables and called `safepoint()` on the caller's context: a
  compiled module with an allocating loop gave wrong results or crashed once
  collections ran, and with protopy's adaptive heap controller collections always
  run. Each compiled call now pays for a context, as an interpreted call does:
  `fib(27)` took about 0.11-0.14 s instead of 0.07-0.08 s (Release, 2026-10-11);
  since the self-recursive call skips the entry point (below), 0.075-0.078 s
  (protoCore 2.14.4).
- **Callables and their module.** Every compiled callable (function, generator
  function and its continuation, lambda) is a native method bound to an immutable
  *function storage* object: `__module_ns__`, the namespace of the module that
  defined it (the current globals at the definition), and the values of its free
  variables, captured at the definition. The callable's entry point enters that
  namespace with `protoPython::ModuleNamespaceScope` (current frame and globals) for
  as long as it runs, so its module-level names resolve in its own module whoever
  calls it, as an interpreted function's do through `__globals__`; then it runs the
  body. Called as a function (`f(a)`, `C.m(obj, a)`, a method call from compiled
  code), it receives its storage as `self`. The interpreter binds a native method
  found on a class to the instance (`obj.m(a)`), so `self` is then the receiver: the
  entry point moves it to the front of the arguments, where the prologue expects the
  method's `self` parameter. A `def` in a class body therefore also binds its storage
  in the class namespace, under a key of its own (`__protopyc_fs_<C++ name>__`), and
  the entry point finds it through the receiver's class: a method keeps its module
  namespace and its free variables however it is called, including a method of a
  class defined inside a function, called on an instance from interpreted code. A
  callable without a storage there (one attached to a class from outside the class
  body, or a lambda in a class body) falls back to the namespace of the module the
  file initialized, with its free variables `None`. A self-recursive call by the
  function's own name calls the body directly. `def` and `class` statements in a
  function bind function locals; names a class body binds go to the class namespace.
  Free variables are transitive, as in Python: a function captures the enclosing
  functions' variables that the functions nested in it use.
- **Exceptions.** As in the interpreter, an operation that raises leaves its
  exception pending in the `PythonEnvironment`. Generated code reads the address of
  the thread's pending-exception flag once per call
  (`PythonEnvironment::pendingExceptionFlagAddress`) and tests the flag after every
  operation that can raise and after every statement; when it is set it throws the
  C++ exception `__PyUnwind`, so neither the rest of the statement (a call's other
  arguments, the call itself, an assignment) nor the rest of its statement list runs.
  A callable catches `__PyUnwind` and returns `nullptr` with the exception still
  pending, which is how a native method reports an exception to its caller; the
  module initializer does the same, so the import fails with the exception.
  - A `try` statement catches `__PyUnwind` around each of its clauses. If the body
    left an exception pending, the first `except` clause whose type matches takes it
    (binding it to the `as` name) and, while it runs, the exception is the one
    `sys.exc_info()` and a bare `raise` see (`PythonEnvironment::pushActiveException`,
    popped however the clause is left); an exception no clause matches stays
    pending. The `else` clause runs when the body raised nothing. The `finally`
    clause runs with the propagating exception set aside in a slot, and that
    exception is pending again afterwards unless the clause raised its own or left
    by `return`, `break` or `continue`.
  - `return`, `break` and `continue` inside a `try` statement with a `finally`
    clause (in its body, an `except` clause or the `else` clause), or inside a `with`
    statement, store their kind in a slot of that statement (a return value in a slot
    of the callable) and jump (`goto`) to its `finally` clause or `__exit__` call;
    after it the exit continues outwards, through any further such statements. A
    `return` in a `finally` clause replaces the exception or return value in
    transit, as in CPython.
  - `raise X` instantiates `X` with no arguments when it is a class, as the
    interpreter's `RAISE_VARARGS` does; `raise X from Y` also sets `X.__cause__`
    to `Y` (an instance of `Y` when it is a class) and `__suppress_context__`; a
    bare `raise` re-raises the exception the innermost running `except` clause
    handles, and raises `RuntimeError` outside one.
- **`with` statements.** `with a as x, b as y:` is `with a as x:` containing
  `with b as y:`. For each item the context manager is evaluated, `__enter__` and
  `__exit__` are looked up on its type, as the interpreter does
  (`PythonEnvironment::lookupBoundSpecialMethod`; `TypeError` when either is
  missing), `__exit__` is kept in a slot, `__enter__` is called and its result bound
  to the target. When the body is left, `__exit__` is called with the exception
  that is propagating -- its type, the exception and its `__traceback__` attribute
  (or `None`) -- or with three `None` when the body completed or left by `return`,
  `break` or `continue`. A true result suppresses the exception and execution
  continues after the `with` statement; otherwise the exception, or one `__exit__`
  raised, propagates.
- **Generators.** A function that contains `yield`, `yield from` or `await` compiles
  to two callables: the function, which binds its arguments and returns a generator
  object, and the continuation (`gi_native_callback`), which runs the body to the
  next yield each time the generator is resumed. `py_generator_send_impl` calls it
  with the generator, the value sent and, for `throw()` and `close()`, the exception
  to raise at the suspended yield. State lives in the generator object, where the
  collector sees it: `gi_pc` is the number of the yield to resume at (0 before the
  first resumption, -1 once finished), `gi_locals` a list of the continuation's
  slots (locals, a `for` loop's iterator, a caught exception, set aside
  exceptions). The yields are numbered in source order; a resumption restores the
  slots and re-enters the body, and every statement list and `if`, `while`, `for`,
  `try` and `with` statement on the path to the suspended yield is re-entered
  without evaluating its test, its iterator or its context expressions again. A
  `return` finishes the generator with its value in `StopIteration`. `yield from`
  delegates as the interpreter's `OP_YIELD_FROM` does: `send()` to the
  sub-iterator (or `__next__` when it has no `send` and the value is `None`),
  `throw()` and `close()` to its `throw`, and its `StopIteration` value is the
  result. Exceptions, `try` and `with` statements work in a generator as in a
  function; the slots recording an exception set aside or an exit in transit are
  saved with the others when the generator suspends.

## 5. Limitations

- A `yield`, `yield from` or `await` must be a statement or the value of an
  assignment (`x = yield v`); one inside another expression (`f((yield v))`) is
  rejected at compile time.
- An exception raised in an `except` clause gets no `__context__` (the interpreter
  does not set it either), and compiled code adds no traceback entries.
- The interpreter does not run a `finally` clause when an exception or a `return`,
  `break` or `continue` leaves an `except` or `else` clause of the same `try`
  statement, nor when no `except` clause matches; compiled code runs it, as CPython
  does (`test/compiler/exceptions_with_cpython.expected`). The interpreter also
  ignores the cause of `raise X from Y`.
- A callable attached to a class from outside its class body, or a lambda in a
  class body, called on an instance has no access to its free variables (they read
  as `None`), and its module-level names resolve in the module the file
  initialized; loading the same library twice under two module names makes both
  use the second.
- Attribute reads (`obj.name` outside a call) are plain protoCore lookups
  (`PythonEnvironment::getAttr`): a missing attribute reads as `None` instead of
  raising `AttributeError`, and descriptors such as properties are not applied.
- A class that protopyc compiles has no `__mro__` of its own: a class the
  interpreter defines with it as a base does not inherit its methods, and the
  interpreter's `with` statement on one of its instances calls `__enter__` and
  `__exit__` without the instance (compiled code's `with` binds them).
- Generator expressions are evaluated eagerly, as a list.

## 6. Not implemented

An earlier version of this specification described features that `protopyc` does not
have:

- a C++ abstraction layer of `py::` types (such as `py::list` or `py::actor`) and an
  actor-based runtime; generated code uses `PythonEnvironment` and protoCore calls;
- compile-time resolution of relative imports;
- static type inference beyond the small-integer fast path;
- C++ namespaces that mirror the Python module hierarchy.
