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

A Python exception is a C++ exception, `protoPython::PyException`, which carries the
Python exception object. A native (C++) operation raises with one of
`PythonEnvironment`'s `raise*` helpers, which throw it; native code that handles an
exception catches it (`catch (const protoPython::PyException& e)`, `e.exception()`),
and an embedder catches whatever the code it runs does not handle (see
[CPP_API_REFERENCE.md](CPP_API_REFERENCE.md#errors)). A Python `try` statement is a C++
`try` in the interpreter and in code protopyc generates; the exception being handled
(`sys.exc_info()`) is kept in the context of the activation that handles it. HPy
extensions see C error codes instead: the HPy calls catch the exception and return the
API's error value ([HPY_USER_GUIDE.md](HPY_USER_GUIDE.md)).
