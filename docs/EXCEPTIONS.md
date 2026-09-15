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

Native (C++) operations report errors without C++ exceptions: they set a pending
exception for the current thread with `PythonEnvironment::setPendingException`, and
callers retrieve it with `takePendingException()` (see
[CPP_API_REFERENCE.md](CPP_API_REFERENCE.md#errors)).
