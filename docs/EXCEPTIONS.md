# The `exceptions` Module

protoPython provides a native module named `exceptions` (registered in
`src/library/PythonEnvironment.cpp`, implemented in `src/library/ExceptionsModule.cpp`).
It exposes the built-in exception hierarchy (`BaseException`, `Exception`,
`ArithmeticError`, `KeyError`, `ValueError` and the other built-in exception types).
The objects are the same as the built-ins: `exceptions.KeyError is KeyError` is `True`.
CPython 3 has no `exceptions` module, so portable code uses the built-in names directly.

```python
import exceptions

e = exceptions.Exception("something went wrong")
k = exceptions.KeyError("missing_key")
print(repr(k))          # KeyError('missing_key')
```

## Exceptions raised by native code

A Python exception is a C++ exception: protoCore's `proto::ProtoException`
(protoCore 3.0.0, the one exception type of every language on protoCore), which
carries the Python exception object as its `value()` and keeps it rooted while it
travels. protoPython throws it through a thin subclass, `protoPython::PyException`
(`PythonEnvironment.h`), that only spells the throw; it adds no state. A native
(C++) operation raises with one of `PythonEnvironment`'s `raise*` helpers or
`raiseException`, which throw it. Native code that handles an exception catches the
base, `catch (const proto::ProtoException& e)`, and reads `e.value()`; it never
catches only `PyException`, because an error a C++ module or another language raises
is a `ProtoException` that is not a `PyException`. An embedder catches whatever the
code it runs does not handle (see [CPP_API_REFERENCE.md](CPP_API_REFERENCE.md#errors)).

A Python `try` statement is a C++ `try` in the interpreter and in code protopyc
generates; the exception being handled (`sys.exc_info()`) is kept in the context of
the activation that handles it. HPy extensions see C error codes instead: the HPy
calls catch the exception and return the API's error value
([HPY_USER_GUIDE.md](HPY_USER_GUIDE.md)).

## An exception no handler selects leaves unchanged

protoCore's rule E3: an exception that no handler selects leaves as the same C++
exception object that entered, so a `ProtoException` subclass another runtime threw
keeps its extras (a traceback, a source position). protoPython never re-raises such an
exception by value:

- Interpreter: an except chain that matches nothing, a `finally` clause and a `with`
  statement whose `__exit__` returns false end in `RERAISE`, a C++ `throw;` inside the
  catch the clause runs in. A bare `raise` rethrows the exception being handled
  (`std::current_exception()`, when it carries the active value).
- protopyc: a clause runs after its C++ `try`, outside the catch (a generator must be
  able to re-enter it), so the catch keeps `std::current_exception()` in a variable of
  the generated function's frame; an unmatched except chain, a `finally` clause, a
  `with` statement and a bare `raise` throw that `std::exception_ptr` again.
- Exceptions crossing generator, `yield from` and call frames propagate as they are;
  an HPy call keeps the exception it caught with the error it reports, and raises it
  again when the extension returns that error.

Two cases do create a new exception with the same value, by design:

- **`raise e`** of a caught value is a new raise in Python (it adds a traceback entry
  and chains as a new raise does), so it throws protoPython's own exception carrying
  the same object; a foreign subclass's C++ extras are not carried over. Use a bare
  `raise` to re-raise unchanged.
- **A generator suspended inside an except or finally clause** that re-raises after it
  is resumed: the C++ exception object does not survive the yield (generator state
  holds values only, in protoCore objects), so the value is thrown again.

`test_exception_identity` (`test/exceptions/`) checks these paths, interpreted and
compiled (`protopyc_exception_identity`), with a `ProtoException` subclass carrying an
extra field.

## How `except` selects an exception

`except T` tests the raised value `v` (`PythonEnvironment::isException`):

| `T` | `except T` catches `v` when | Otherwise |
|-----|-----------------------------|-----------|
| a tuple | any element catches `v` | |
| a Python exception class (`BaseException` or a subclass) | `v` is an instance of `T` (its prototype chain contains `T`), as CPython's `isinstance`; a foreign value (below) is caught by `BaseException` and `Exception` only | |
| a foreign type: a protoCore object that is not a Python object (a C++ module's error prototype, another language's class) | `T` is on `v`'s prototype chain (`ProtoObject::isInstanceOf`) | |
| any other Python object (`int`, `object`, an instance) | | `TypeError: catching classes that do not inherit from BaseException is not allowed`, as CPython raises when the clause is evaluated |

A bare `except:` catches every value.

### Foreign errors

A *foreign value* is a thrown value that is not an instance of `BaseException`: an
error a C++ module or another language running on protoCore raised. To Python code it
is an ordinary error: `except Exception`, `except BaseException` and a bare `except:`
catch it (it is never treated like `SystemExit` or `KeyboardInterrupt`), and a handler
that names the module's own type selects it by prototype. The value reaches the
handler unchanged:

- `except T as e` binds the value itself; `e.message`, `e.name` and any other
  attribute are read from it and its prototype chain.
- `type(e)` is its prototype (the module's type), `str(e)` is its `message`
  attribute when that is a string, and `repr(e)` is `Name('message')`, `Name` being
  its `name` attribute (`ForeignError` when it has none).
- `sys.exc_info()` and a bare `raise` see the same value; `raise X from e` and an
  exception raised while handling it record it as `__cause__` / `__context__`.
- protoPython never writes to a foreign value: it gets no `__traceback__`,
  `__context__` or `__cause__` (it belongs to its module, and may be immutable or
  shared with another language). Its report is therefore one line, without
  traceback entries.

```python
import example.geometry as geometry        # a C++ UMD module (logical path example/geometry)

try:
    geometry.circleArea(-1)
except geometry.NegativeRadius as e:       # the module's own type, selected by prototype
    print(type(e) is geometry.NegativeRadius, e.message)
except geometry.GeometryError:             # its base type would catch it too
    pass
```

### Importing a UMD module

A dotted name that no Python provider resolves (no `.py` file, package, compiled
module or HPy extension) is looked up as a protoCore UMD logical path, with the dots
as slashes: `import example.geometry` loads the module registered under
`example/geometry` through the space's resolution chain
(`ProtoSpace::getImportModule`). A module that a provider returns without `__name__`
or `__executed__` (that is, not one of protoPython's own modules) is a foreign module;
it is imported once as a Python module object holding its attributes, the same
objects, so `except geometry.NegativeRadius` names the very prototype the module
raises. Its methods are bound to the foreign module, because a UMD method receives its
module as `self`; `__protocore_module__` is the module object itself. A parent package
that does not exist becomes an empty namespace package (PEP 420), so
`import example.geometry as geometry` and `from example import geometry` both work.

The provider must be registered with protoCore's `ProviderRegistry` and named in the
resolution chain (`provider:<alias>`) by the program that embeds protoPython. `protopy`
has no plug-in loader; the test suite's `protopy_umd_host` (`test/umd/`) links
protoCore's example geometry module, registers it and appends
`provider:example_geometry` to the chain:

```bash
build_release/protopy_umd_host test/regression/umd_geometry_errors.py
```

## protoCore's own errors

protoCore raises its own failures (an integer division by zero, a malformed integer
literal, ...) as `ProtoException`s whose value the space's error factory builds.
protoPython installs its factory (`PythonEnvironment::pythonErrorFactory`) when the
environment starts, so they arrive as Python's built-in exceptions, built as the
Python constructor builds them (`args == (message,)`):

| `proto::ProtoErrorKind` | Python exception | Message |
|-------------------------|------------------|---------|
| `DivisionByZero` | `ZeroDivisionError` | `division by zero` (CPython 3.14's text) |
| `Overflow` | `OverflowError` | protoCore's message |
| `InvalidArgument` | `ValueError` | protoCore's message |
| `TypeMismatch` | `TypeError` | protoCore's message |
| `ArityMismatch` | `TypeError` | protoCore's message |

The interpreter tests most of these conditions itself before it calls protoCore and
raises CPython's own message (`int('x')` is `invalid literal for int() with base 10:
'x'`); the factory covers what reaches protoCore anyway, such as a C++ module that
parses an integer (`geometry.parseRadius("ten")` raises `ValueError` with protoCore's
message). Where protoPython converts a large integer to 64 bits and falls back to the
big-integer form when it does not fit, it tests for protoCore's `Overflow` error
(`PythonEnvironment::tryAsLong`, `rethrowUnlessOverflow`) and lets every other
exception propagate. On a thread that runs no protoPython environment the factory
answers protoCore's default value (a child of `ProtoSpace::getErrorPrototype(kind)`
with a `message`).

## C++ exceptions from native code

A C++ standard exception (`std::exception` and its subclasses) thrown by native code
is a defect of that code, not a Python value. It is translated where native code is
called: by the interpreter's per-instruction boundary, by `invokePythonCallable` (the
call path of compiled code and of native callers), and by the HPy API functions. It
continues as `RuntimeError(what())`: `geometry.legacyArea(-1)`, which throws
`std::runtime_error("legacyArea: negative radius")`, raises `RuntimeError('legacyArea:
negative radius')`. `ProtoException` does not derive from `std::exception`, so a Python
exception and a foreign error pass these boundaries untouched.

## The top level

- `protopy` (script, `-m`, `-c`, standard input) reports an unhandled exception the
  Python way, as CPython does (traceback, then `Type: message`; a foreign value as
  `Name: message`), and exits with status 1; `SystemExit` sets the exit status. A C++
  exception that escapes every Python frame is reported as `protopy: unhandled C++
  exception ...` instead of reaching `std::terminate`.
- The REPL reports each statement's exception and continues.
- An exception that leaves the function of a thread started with
  `_thread.start_new_thread` is reported through `sys.unraisablehook` ("Exception
  ignored in thread started by: <function>" and its traceback), as CPython does;
  `SystemExit` ends the thread silently. (`threading.Thread` reports its target's
  exceptions itself, through `threading.excepthook`.) A C++ exception is reported on
  standard error. Before this, such an exception was dropped without a report.
- An embedder that calls `executeString`, `executeModule` or `callObject` receives an
  exception the code does not handle as the `proto::ProtoException` carrying the Python
  exception object (or the foreign value), never as a string or a
  `std::runtime_error`. It must catch it while the `PythonEnvironment` is alive: the
  exception pins its value in the environment's space.
