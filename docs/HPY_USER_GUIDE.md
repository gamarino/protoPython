# Loading HPy-Style Extension Modules

> **Status (2026-09-15).** `PythonEnvironment` registers `HPyModuleProvider`, so
> `import foo` loads an HPy-style extension module `foo.hpy.so` (or `foo.so`) that
> exports `HPyInit_foo`. The extension API is a protoPython-specific C++ subset, not
> the HPy universal ABI: modules built with the HPy SDK do not load. The API is
> described in [HPY_DEVELOPER_GUIDE.md](HPY_DEVELOPER_GUIDE.md).

## Module resolution

`PythonEnvironment` registers four module providers with protoCore's provider
registry, tried in this order:

- `NativeModuleProvider`: modules implemented in C++ in `src/library/`;
- `CompiledModuleProvider`: shared libraries produced by `protopyc`, which export
  `proto_module_init` (see [PROTOPYC_SPECIFICATION.md](PROTOPYC_SPECIFICATION.md));
- `HPyModuleProvider`: HPy-style extension modules;
- `PythonModuleProvider`: `.py` source modules.

A `foo.so` that exports `proto_module_init` is therefore loaded as a compiled module
before the HPy loader looks at it. The directories the providers search come from the
command line and the environment; see [USER_GUIDE.md](USER_GUIDE.md#module-search-path).

## What `HPyModuleProvider` does

For a module name, `HPyModuleProvider` (`src/library/HPyModuleProvider.cpp`):

1. replaces dots in the name with `/` and, in each search directory, looks for
   `<name>.hpy.so` and then `<name>.so`;
2. opens the file with `dlopen(path, RTLD_NOW | RTLD_LOCAL)`;
3. looks up the symbol `HPyInit_<name>` with `dlsym`, where `<name>` is the last
   component of a dotted module name; if it is missing, the library is closed again
   and the import fails with `ModuleNotFoundError`;
4. calls it with a temporary `HPyContext` created for the current `ProtoContext`;
   handles created during the call are pinned as GC roots until it returns;
5. sets `__file__`, `__name__` and `__loader__` on the returned module object and
   marks it initialised. The module is then cached in `sys.modules` like any other.

A library stays loaded until the process exits; reloading or unloading an extension
module is not supported. If any step fails the provider returns no module and prints
no diagnostics. The `PROTO_HPY_DEBUG` variable does not change this: it is compiled
out of Release builds, and even in diagnostic builds the loader emits no messages.

## Limitations

- Module functions receive positional arguments only; keyword arguments are ignored.
- A function that returns `HPy_NULL` returns `None` to Python. `HPyErr_SetString`,
  `HPyErr_Occurred` and `HPyErr_Clear` are stubs, so an extension cannot raise a
  Python exception.
- `HPyList_New`, `HPyTuple_New` and `HPyDict_New` create raw protoCore collections
  rather than Python `list`, `tuple` and `dict` objects, and `HPyList_Append` does not
  change the list.
- `HPy_AsUTF8` returns a pointer into one shared buffer that the next call overwrites.
- `HPyField` and `HPyGlobal` are not implemented; an extension that keeps an object
  beyond a call must pin it in a `ProtoRootSet` of its own (see
  [GC_BRIDGING.md](GC_BRIDGING.md#hpy-extensions)).

The tested subset is module creation, module functions and the handle and number
operations used by `examples/hpy/math_hpy.cpp` (CTest `protopy_hpy_module_basics`,
`protopy_hpy_stress` and `test_hpy_context`).

## Troubleshooting a module that fails to load

- The file name must match the init function: `foo.hpy.so` or `foo.so` must export
  `HPyInit_foo`.
- In C++ sources, declare the init function `extern "C"` so that its name is not
  mangled.
- Check for missing shared-library dependencies with `ldd`.
