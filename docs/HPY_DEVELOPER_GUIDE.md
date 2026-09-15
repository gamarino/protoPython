# HPy-Style Extension API

protoPython contains a C++ extension API modelled on [HPy](https://hpyproject.org/).
This guide describes the API as it exists in the source tree and how an extension
module is written against it.

> **Status (2026-09-15).** The API is a protoPython-specific C++ subset, not the HPy
> universal ABI: modules built with the HPy SDK are not compatible with it. Modules
> written against it are imported through `HPyModuleProvider`; see
> [HPY_USER_GUIDE.md](HPY_USER_GUIDE.md), which also lists the API's limitations. The
> API is exercised by the `test_hpy_context` unit test and by the `math_hpy` example
> module (`protopy_hpy_module_basics`, `protopy_hpy_stress`).

## API overview

The API is declared in `include/protoPython/HPyContext.h` and
`include/protoPython/HPyABI.h`, in namespace `protoPython`, and implemented in
`src/library/HPyContext.cpp`.

- `HPy` is an opaque handle (`unsigned long`); `HPy_NULL` (0) is the invalid handle.
- `HPyContext` is a C++ struct holding a `proto::ProtoContext*` and a table that maps
  handles to `ProtoObject*`; every open handle is pinned as a GC root until `HPy_Close`
  or the end of the call.
- Module functions created by `HPyModule_Create` receive the module as `self`.
- `HPyCFunction` is `HPy (*)(HPyContext* ctx, HPy self, const HPy* args, size_t nargs)`.
- `HPyMethodDef` has the fields `ml_name`, `ml_meth`, `ml_flags` and `ml_doc`;
  `HPyModuleDef` has `m_name`, `m_doc`, `m_size` and `m_methods`.

Functions declared in `HPyContext.h`:

| Area | Functions |
|------|-----------|
| Handles | `HPy_FromPyObject`, `HPy_AsPyObject`, `HPy_Dup`, `HPy_Close` (`HPy_Incref` and `HPy_Decref` are no-ops) |
| Attributes | `HPy_GetAttr`, `HPy_SetAttr`, `HPy_GetAttr_s`, `HPy_SetAttr_s` |
| Calls | `HPy_Call`, `HPy_CallMethod` |
| Types and objects | `HPy_Type`, `HPy_New`, `HPyType_FromSpec` |
| Values | `HPy_FromLong`, `HPy_FromDouble`, `HPy_FromUTF8`, `HPy_AsLong`, `HPy_AsDouble`, `HPy_AsUTF8`, `HPy_IsTrue` |
| Protocols | `HPy_Add`, `HPy_Sub`, `HPy_Mul`, `HPy_Div`, `HPy_And`, `HPy_Or`, `HPy_Xor`, `HPy_LShift`, `HPy_RShift`, `HPy_RichCompare`, `HPy_GetItem`, `HPy_SetItem`, `HPy_Length`, `HPy_Contains`, `HPy_GetIter`, `HPy_Next` |
| Collections | `HPyList_New`, `HPyList_Append`, `HPyDict_New`, `HPyDict_SetItem`, `HPyDict_GetItem`, `HPyTuple_New`, `HPyTuple_Pack`, `HPySlice_New` |
| Modules | `HPyModule_Create`, `HPyModule_AddObject`, `HPyModule_AddStringConstant`, `HPyModule_AddIntConstant` |
| Errors | `HPyErr_SetString`, `HPyErr_NewException`, `HPyErr_Occurred`, `HPyErr_Clear` |
| Debugging | `HPy_Dump` |

## Writing a module

The repository's example is `examples/hpy/math_hpy.cpp`:

```cpp
#include <protoPython/HPyABI.h>
#include <stdio.h>

using namespace protoPython;

static HPy add_values(HPyContext* ctx, HPy self, const HPy* args, size_t nargs) {
    if (nargs < 2) return HPy_NULL;
    return HPy_Add(ctx, args[0], args[1]);
}

static HPyMethodDef MathMethods[] = {
    {"add", add_values, 0, "Adds two values"},
    {NULL, NULL, 0, NULL}
};

static HPyModuleDef moduledef = {
    "math_hpy",
    "Math extension for HPy",
    -1,
    MathMethods
};

extern "C" HPy HPyInit_math_hpy(HPyContext* ctx) {
    return HPyModule_Create(ctx, &moduledef);
}
```

The init function must be named `HPyInit_<name>`, where `<name>` is the last component
of the module name, and must have C linkage.

## Building

Modules are C++20 source. The headers include `protoCore.h`, so both header directories
are needed:

```bash
g++ -std=c++20 -shared -fPIC \
  -I<protoPython>/include -I<protoCore>/headers \
  -o math_hpy.hpy.so examples/hpy/math_hpy.cpp
```

The API functions are defined in libprotoPython (`src/library/HPyContext.cpp`). The
project's CMake files build the example as `build_release/test/hpy/math_hpy.hpy.so`,
which the HPy tests import with `protopy -p build_release/test/hpy`.
