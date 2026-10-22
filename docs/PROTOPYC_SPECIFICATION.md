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
  defined it (the current globals at the definition), the cells of its free
  variables (below) and its default values, evaluated once at the definition
  (`__defaults__`, a list for the last positional parameters, and `__kwdefaults__`
  for keyword-only ones). The callable's entry point enters that namespace with
  `protoPython::ModuleNamespaceScope` (current frame and globals) for as long as it
  runs, so its module-level names resolve in its own module whoever calls it, as an
  interpreted function's do through `__globals__`; then it runs the body. Called as
  a function (`f(a)`, `C.m(obj, a)`), it receives its storage as `self`. Found on a
  class through an instance, it binds as a Python function does
  (`PythonEnvironment::isCompiledFunction` recognizes it by the storage's own
  `__module_ns__`; `bindNativeMethod` binds it): `obj.m(a)` calls it with the storage
  as `self` and `obj` as its first argument, wherever the interpreter binds a method
  (attribute reads and method calls, `super()`, special-method lookups such as
  `with`'s, a `__getattribute__` override). So a method, a lambda in a class body and
  a function attached to a class from outside it keep their module namespace and
  their free variables however they are called. Where a runtime path still rebinds
  the native method to the instance, `self` is the receiver: the entry point moves it
  to the front of the arguments and finds the storage through the receiver's class,
  under a key a `def` in a class body also binds (`__protopyc_fs_<C++ name>__`; it is
  runtime bookkeeping, which `vars(cls)`, `cls.__dict__` and `dir()` do not show);
  without one, it falls back to the namespace of the module the file initialized,
  and its free variables read as unbound. `def` and `class` statements in a function
  bind function locals; names a class body binds go to the class namespace.
- **Scopes and closures.** `CppGenerator::buildScopes` builds a symbol table before
  any code is emitted: for the module, each `def`, each lambda and each class body,
  the names it binds (assignment, augmented assignment, `for` targets, `with ... as`,
  `except ... as`, imports, `def` and `class`, `del`, walrus, `match` captures), the
  names it declares `global` or `nonlocal`, and the names it reads. Free variables
  are resolved through the enclosing functions, skipping class bodies as Python
  does, and passed through the scopes in between; a function's cell variables are
  its locals that a nested scope uses. (Comprehensions belong to the scope that
  contains them, since protopyc emits them inline.) A function's slots hold its
  parameters first (positional, keyword-only, `*args`, `**kwargs`), then its other
  locals, then its free variables. A cell variable holds a cell in its slot
  (`PythonEnvironment::newCell`, the interpreter's cell); a free variable holds the
  cell it closes over, read from the function storage at entry. The storage keeps
  cells, never values, so a read sees the variable's current binding (late
  binding), `nonlocal` writes and `del` reach the scope that defined it, and an
  empty cell raises `NameError` or `UnboundLocalError` with CPython's message. A
  class body reads a free variable from its namespace when the namespace binds it,
  else from the cell. A recursive `def` whose name only that `def` binds in its
  scope (not decorated, not inside a loop) calls its body directly when it calls
  itself by name, and does not capture its own name unless its body uses the name
  other than as a plain call, or a scope nested in it does: so it closes no cycle
  between mutable objects (the cell, the method, its storage) and a loop of calls
  defining it runs in a bounded heap. A rebound or decorated name is called through
  its binding, as Python does.
- **Arguments.** A call binds as CPython binds (`PythonEnvironment::bindArguments`,
  the same binding the interpreter uses, `bindArgumentsCore`): positional and
  keyword arguments, the defaults stored at the definition, keyword-only and
  positional-only parameters, `*args` (a tuple) and `**kwargs` (a dict in call
  order), and CPython's `TypeError`s (missing, too many, unexpected keyword,
  multiple values, positional-only passed as keyword). A call with exactly the
  positional parameters of a function without keyword-only parameters, `*args` or
  `**kwargs` binds them directly. Tuple and list targets unpack as CPython does
  (`a, b = v`, `a, *rest = v`, in assignments and `for` loops).
- **Attributes.** `obj.name` reads through `PythonEnvironment::loadAttr`, the read
  the interpreter's `LOAD_ATTR` and `getattr()` do: data descriptors,
  `__getattribute__`, instance attributes, non-data descriptors (properties,
  methods bound to `obj`), then `__getattr__`; a missing attribute raises
  `AttributeError`. An own attribute of an ordinary instance whose type does not
  override `__getattribute__` and that is not a descriptor is read first through
  protoCore's per-thread attribute cache, as the interpreter's `LOAD_ATTR` reads it
  (`PythonEnvironment::ownAttributeFast`). `obj.name = value` writes through
  `storeAttr` (data descriptors, `__setattr__`, `__slots__`), and `obj.m(args)`
  calls through `callMethod(ctx, obj, name, {args})` (the name interned once at the
  call site), which calls a function found on obj's class with `obj` first, without
  a bound method, as `LOAD_METHOD` and `CALL` do, and anything else as it is. `a in b`
  and `a not in b` run the interpreter's `COMPARE_OP`. `env->getAttr` / `setAttr`,
  the raw protoCore read and write generated code used before, remain for
  embedders.
- **Classes.** A `class` statement builds its class as the interpreter's
  `BUILD_CLASS` does (`PythonEnvironment::buildClass`, shared by both): PEP 560
  `__mro_entries__`, the metaclass (explicit or derived from the bases) and its
  `__prepare__`; the class body then runs with that namespace as its frame
  (`__qualname__` is set first, `name: annotation` statements fill
  `__annotations__`), and the metaclass call makes the class, with its own
  `__mro__`, `__init_subclass__` and `__set_name__`. A compiled class is therefore a
  Python class: an interpreted class can derive from it and inherit its methods,
  `super()` reaches its methods, and it can be an exception, an `Enum`, a dataclass
  or have `__slots__`. Zero-argument `super()` in a method is
  `super(<class>, <first parameter>)`, and `__class__` in a method is that class:
  the method's storage keeps the class's cell, which the class body leaves in its
  namespace as `__classcell__` and `type.__new__` fills with the class before
  `__init_subclass__` runs, as in CPython.
- **Exceptions.** As in the interpreter, a Python exception is a C++ exception,
  protoCore's `proto::ProtoException` (thrown as `protoPython::PyException`), and
  every generated handler catches the base, so an error a C++ module or another
  language raises reaches the `except` clauses too: an operation that raises throws it, so neither the
  rest of the statement (a call's other arguments, the call itself, an assignment)
  nor the rest of its statement list runs, and a compiled callable lets it propagate
  to its caller; the module initializer does the same, so the import fails with the
  exception. Generated code tests nothing after an operation.
  - A `try` statement is a C++ `try`. The exception its body raises is caught and
    kept in a slot, and the handlers run after the C++ `try`, outside the catch
    block, so that a generator suspended in one can re-enter it: the first `except`
    clause whose type matches takes it (binding it to the `as` name) and, while it
    runs, the exception is the one `sys.exc_info()` and a bare `raise` see (kept in
    the callable's context, `PythonEnvironment::HandledExceptionScope`, and restored
    however the clause is left); an exception no clause matches is thrown again. The
    `else` clause runs when the body raised nothing. The `finally` clause runs with
    the propagating exception set aside in a slot, and that exception is thrown again
    afterwards unless the clause raised its own or left by `return`, `break` or
    `continue`. While the `finally` clause runs for an exception, that exception is
    the one being handled (`sys.exc_info()`). An exception an `except` or `finally`
    clause (or an `__exit__` call) raises gets the exception being handled as
    `__context__` (PEP 3134, `PythonEnvironment::chainException`), as in the
    interpreter.
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
- Compiled code adds no traceback entries.
- Generator expressions are evaluated eagerly, as a list.
- Comprehensions run inline in the scope that contains them: their loop
  variables are that scope's variables (CPython gives a comprehension a scope of
  its own).
- A recursive nested function whose body uses its own name other than to call it
  (`f` passed as a value, `map(f, xs)`), or whose nested scopes use the name, closes
  over the cell holding it: one cycle between mutable objects per call of the
  function that defines it, which protoCore never collects (protoCore
  `docs/MemoryModel.md`, section 7). So do mutually recursive nested functions and a
  decorated recursive one, as in the interpreter (`docs/INTERNALS_DEEP_DIVE.md`,
  "Closures").
- Zero-argument `super()` and `__class__` work in a method (a `def` directly in a
  class body), not in a function nested in one.
- A compiled function object has no `__closure__`, `__code__` or `__defaults__`
  attributes: it is a native method.

## 6. Not implemented

An earlier version of this specification described features that `protopyc` does not
have:

- a C++ abstraction layer of `py::` types (such as `py::list` or `py::actor`) and an
  actor-based runtime; generated code uses `PythonEnvironment` and protoCore calls;
- compile-time resolution of relative imports;
- static type inference beyond the small-integer fast path;
- C++ namespaces that mirror the Python module hierarchy.
