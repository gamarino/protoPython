# HPy Developer Guide for protoPython

This guide explains how to write and build HPy extension modules for `protoPython`.

## Overview

`protoPython` supports HPy **Universal ABI** modules. This means you can compile your extension once into a `.hpy.so` (or `.so`) file, and it will run on `protoPython` without recompilation, provided it uses the supported HPy API subset.

## Writing a Module

A minimal HPy module consists of an initialization function and a module definition.

### 1. Include the HPy ABI
Include `<protoPython/HPyABI.h>` (or the standard `hpy.h` if building with the HPy SDK).

### 2. Define your Functions
```c
static HPy hello(HPyContext *ctx, HPy self, const HPy *args, size_t nargs) {
    return HPy_FromUTF8(ctx, "Hello from HPy on protoPython!");
}
```

### 3. Define Methods
```c
static HPyMethodDef MyMethods[] = {
    {"hello", hello, HPy_METH_O, "Say hello"},
    {NULL, NULL, 0, NULL}
};
```

### 4. Define the Module
```c
static HPyModuleDef moduledef = {
    .m_name = "my_hpy_module",
    .m_doc = "A minimal HPy module",
    .m_size = -1,
    .m_methods = MyMethods
};
```

### 5. Initialization Function
The entry point must be named `HPyInit_<modulename>`.
```c
HPy HPyInit_my_hpy_module(HPyContext *ctx) {
    return HPyModule_Create(ctx, &moduledef);
}
```

## Supported API Subset

`protoPython` currently implements a significant subset of the HPy API:
- **Handles**: `HPy_Dup`, `HPy_Close`.
- **Objects**: `HPy_New`, `HPy_FromLong`, `HPy_FromDouble`, `HPy_FromUTF8`.
- **Attributes**: `HPy_GetAttr`, `HPy_SetAttr`, `HPy_GetAttr_s`, `HPy_SetAttr_s`.
- **Calling**: `HPy_Call`, `HPy_CallMethod`.
- **Types**: `HPy_Type`, `HPyType_FromSpec`.
- **Protocols**: `HPy_Add`, `HPy_Sub`, `HPy_Mul`, `HPy_Div`, `HPy_GetItem`, `HPy_SetItem`, `HPy_Length`, `HPy_Contains`.
- **Exceptions**: `HPyErr_SetString`, `HPyErr_Occurred`, `HPyErr_Clear`.

## Building the Module

Use `gcc` to compile your C code into a shared library.

```bash
gcc -shared -fPIC -I/path/to/protoPython/include -o my_hpy_module.hpy.so my_module.c
```

Notice that you don't need to link against `protoPython` itself; the `HPyContext` provided at runtime contains all the necessary function pointers for the ABI.
