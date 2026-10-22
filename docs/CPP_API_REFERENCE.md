# C++ API Reference

This page describes the C++ interface for embedding protoPython in a C++ program. The
declarations are in `include/protoPython/PythonEnvironment.h`; values are protoCore
objects declared in protoCore's
[`headers/protoCore.h`](https://github.com/numaes/protoCore/blob/master/headers/protoCore.h).
`src/runtime/main.cpp` (the `protopy` executable) is a complete, working example of the
calls described here. `examples/embedding_sample.cpp` is a smaller one: it runs source
code in `__main__` and reads a global back. It is built by default (CMake option
`PROTOPYTHON_BUILD_EXAMPLES`) and run by CTest as the `embedding_sample` test.

```cpp
#include <protoPython/PythonEnvironment.h>
```

## `protoPython::PythonEnvironment`

`PythonEnvironment` holds the runtime state: built-in types, `builtins`, `sys` and
module resolution.

### Construction

```cpp
PythonEnvironment(const std::string& stdLibPath = "",
                  const std::vector<std::string>& searchPaths = {},
                  const std::vector<std::string>& argv = {});
```

- `stdLibPath`: the standard library directory; when empty the environment tries its
  defaults.
- `searchPaths`: module search directories.
- `argv`: the contents of `sys.argv`.

Every environment uses the process-wide `proto::ProtoSpace` returned by the static
`PythonEnvironment::getProcessSpace()`. `getSpace()` returns that space and
`getContext()` returns the environment's root `proto::ProtoContext`.

### Running code

| Member | Result |
|--------|--------|
| `int executeString(const std::string& source, const std::string& name = "<string>")` | Runs source code in the `__main__` context and returns 0. An exception the code does not handle propagates as the `proto::ProtoException` carrying it. |
| `int executeModule(const std::string& moduleName, bool asMain = false, proto::ProtoContext* ctx = nullptr)` | Resolves and runs a module (as `__main__` when `asMain` is true). Returns 0 on success, -1 if the module cannot be resolved, -2 on a runtime failure, -3 when the module raised `SystemExit` (the status is in `getExitRequested()`). |
| `void runRepl(std::istream& in = std::cin, std::ostream& out = std::cout)` | Runs the interactive loop. |
| `int getExitRequested() const` | Exit status requested through `SystemExit` or `sys.exit`. |
| `void runExitHandlers()` | Runs the functions registered with `atexit`. |
| `void setExecutionHook(ExecutionHook hook)` | Installs a callback `(moduleName, phase)` called before (phase 0) and after (phase 1) module execution. |

### Errors

A Python exception is a C++ exception: a call that raises throws protoCore's
`proto::ProtoException` (protoCore 3.0.0), whose `value()` is the Python exception
object, kept reachable by the collector while the exception exists. protoPython throws
it through `protoPython::PyException` (`PythonEnvironment.h`), a subclass that adds
nothing; catch the base, `const proto::ProtoException&`, because an error a C++ module
or another language raises is a `ProtoException` too (its value is then not a Python
exception: see [EXCEPTIONS.md](EXCEPTIONS.md#foreign-errors)). A native function raises
with one of the `raise*` helpers or `raiseException`, which throw it. There is no
pending-exception state to test or clear. Catch it while the `PythonEnvironment` is
alive: the exception pins its value in the environment's space. `ProtoException` does
not derive from `std::exception`; a C++ standard exception thrown by native code
reaches Python code as `RuntimeError(what())`.

| Member | Result |
|--------|--------|
| `[[noreturn]] void raiseException(const proto::ProtoObject* exc)` | Raises `exc`: throws it as a `PyException` (a `proto::ProtoException`). |
| `bool isException(const proto::ProtoObject* exc, const proto::ProtoObject* type)` | Whether `except type:` catches `exc`: `type` a Python exception class (CPython's `isinstance`; a foreign value matches `BaseException` and `Exception`), a foreign type (by prototype chain) or a tuple of them. Raises `TypeError` for any other Python object, as CPython. |
| `bool isPythonException(proto::ProtoContext* ctx, const proto::ProtoObject* value) const` | Whether `value` is an instance of `BaseException` (otherwise it is a foreign value). |
| `const proto::ProtoObject* newExceptionInstance(proto::ProtoContext* ctx, const proto::ProtoObject* type, const std::string& message)` | `type(message)`, built without raising. |
| `static bool tryAsLong(proto::ProtoContext* ctx, const proto::ProtoObject* integer, long long& out)` | `asLong`, false when the integer does not fit in 64 bits (protoCore's `Overflow` error). |
| `const proto::ProtoObject* getActiveException(proto::ProtoContext* ctx) const` | The exception being handled where `ctx` runs (`sys.exc_info()`), or `nullptr`. |
| `void handleException(const proto::ProtoObject* exc, const proto::ProtoObject* frame = nullptr, std::ostream& out = std::cerr)` | Formats the exception and its traceback to `out`. For `SystemExit` it writes nothing and records the exit status. |

A minimal embedding, following what `protopy -c` does:

```cpp
#include <protoPython/PythonEnvironment.h>
#include <iostream>
#include <sstream>

int main() {
    protoPython::PythonEnvironment env("", {"."}, {"embedded"});
    try {
        env.executeString("print('hello from protoPython')", "<embedded>");
    } catch (const proto::ProtoException& e) {
        std::ostringstream text;
        env.handleException(e.value(), nullptr, text);
        if (text.str().empty())
            return env.getExitRequested();   // SystemExit
        std::cerr << text.str();
        return 70;
    }
    return 0;
}
```

### Working with Python objects

| Member | Result |
|--------|--------|
| `const proto::ProtoObject* importModule(const std::string& name, int level = 0, const std::vector<std::string>& fromList = {})` | Imports a module. |
| `const proto::ProtoObject* loadAttr(const proto::ProtoObject* obj, const char* name)` | `obj.name` as Python reads it (descriptors, `__getattribute__`, `__getattr__`); raises `AttributeError` when missing. |
| `bool storeAttr(const proto::ProtoObject* obj, const char* name, const proto::ProtoObject* value)` | `obj.name = value` as Python writes it (data descriptors, `__setattr__`); what they raise propagates. |
| `const proto::ProtoObject* getAttr(const proto::ProtoObject* obj, const std::string& attr)` | Raw protoCore attribute read: no descriptors, a missing attribute reads as `None`. |
| `void setAttr(const proto::ProtoObject* obj, const std::string& attr, const proto::ProtoObject* val)` | Raw protoCore attribute write. |
| `const proto::ProtoObject* getItem(const proto::ProtoObject* container, const proto::ProtoObject* key, proto::ProtoContext* ctx = nullptr)` | `container[key]`. |
| `void setItem(const proto::ProtoObject* container, const proto::ProtoObject* key, const proto::ProtoObject* value, proto::ProtoContext* ctx = nullptr)` | `container[key] = value`. |
| `const proto::ProtoObject* callObject(const proto::ProtoObject* callable, const std::vector<const proto::ProtoObject*>& args)` | Calls a Python callable with positional arguments. |
| `const proto::ProtoObject* callMethod(const proto::ProtoObject* obj, const std::string& attr, const std::vector<const proto::ProtoObject*>& args)` | `obj.attr(*args)`: the attribute read as for a call (a function found on obj's class receives `obj` first). |
| `const proto::ProtoObject* iter(const proto::ProtoObject* obj)`, `next(...)` | Iteration protocol. |

`PythonEnvironment::getCurrentContext()` (static) returns the `proto::ProtoContext` of
the current thread.

## protoCore values

Python values are `const proto::ProtoObject*`. The conversions below are members of
protoCore's `ProtoObject`, `ProtoContext` and `ProtoString`, not of
`PythonEnvironment`:

```cpp
proto::ProtoContext* ctx = protoPython::PythonEnvironment::getCurrentContext();

const proto::ProtoObject* n = ctx->fromInteger(42);       // also fromLong, fromDouble, fromBoolean
const proto::ProtoObject* s = ctx->fromUTF8String("text");

if (n->isInteger(ctx)) {
    long long v = n->asLong(ctx);
}
if (proto::ProtoObject::isStringTagFast(s)) {
    std::string out;
    s->asString(ctx)->toUTF8String(ctx, out);
}
```

`ProtoObject::isString(ctx)` implements protoCore's full string protocol;
`ProtoObject::isStringTagFast(obj)` is a tag-only test that accepts heap, interned
(symbol) and inline strings. The constants `PROTO_NONE`, `PROTO_TRUE` and
`PROTO_FALSE` are the Python `None`, `True` and `False`.

## Keeping objects alive

protoCore's garbage collector does not scan C++ stack variables. Native code that keeps
a `ProtoObject*` in a local variable across a call that can run Python code or allocate
must pin it, for example with the RAII helper
`protoPython::PythonEnvironment::TransientPin pin(&env, obj);`. The rules are described
in [GC_BRIDGING.md](GC_BRIDGING.md); the known sites are listed in
[audits/03-gc-roots.md](audits/03-gc-roots.md).

## Native extensions

protoPython contains an HPy-style C++ extension API; `import` loads modules written
against it through `HPyModuleProvider`. See [HPY_DEVELOPER_GUIDE.md](HPY_DEVELOPER_GUIDE.md).
