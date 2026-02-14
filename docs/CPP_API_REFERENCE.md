# C++ API Reference

This document provides a reference for developers who want to embed **ProtoPython** into C++ applications or extend it with native code.

## Core Header

```cpp
#include <protoPython/PythonEnvironment.h>
```

## `PythonEnvironment` Class

The `PythonEnvironment` is the central hub for the Python runtime state.

### Initialization

```cpp
static PythonEnvironment* initialize(proto::ProtoSpace* space);
```
Initializes the Python environment within a `ProtoSpace`. This sets up built-in modules and core types.

### Execution

```cpp
const proto::ProtoObject* executeModule(
    proto::ProtoContext* ctx, 
    const std::string& name, 
    const std::string& code, 
    const proto::ProtoObject* globals = nullptr
);
```
Executes a Python module string.
- `ctx`: Active ProtoContext.
- `name`: Module name (e.g., `"__main__"`).
- `code`: Python source code as a string.
- `globals`: (Optional) Object to use as the global namespace.

### Object Creation Helpers

```cpp
const proto::ProtoObject* fromLong(proto::ProtoContext* ctx, long value);
const proto::ProtoObject* fromString(proto::ProtoContext* ctx, const std::string& val);
```
Utility methods to convert C++ types to ProtoPython objects.

## `ProtoContext` and Memory

All Python operations must happen within an active `proto::ProtoContext`. Contexts manage short-lived objects and facilitate zero-copy promotion to parent contexts (e.g., returning from a function).

### Obtaining a Context

```cpp
proto::ProtoContext* ctx = space.getContext();
```

## Type Conversion (TypeBridge)

ProtoPython objects are `const proto::ProtoObject*`. You can inspect and convert them using:

```cpp
if (obj->isLong(ctx)) {
    long val = obj->asLong(ctx);
}
if (obj->isString(ctx)) {
    std::string s = obj->asString(ctx)->toUTF8String();
}
```

## Error Handling

ProtoPython uses C++ exceptions to signal runtime errors.

```cpp
try {
    env->executeModule(ctx, "test", code);
} catch (const std::exception& e) {
    std::cerr << "Caught Python error: " << e.what() << std::endl;
}
```

## Extension with HPy

For high-performance extensions, it is recommended to use the **HPy API**. This ensures your native code is GIL-free and compatible with various Python runtimes. See [HPy Developer Guide](HPY_DEVELOPER_GUIDE.md) for details.
