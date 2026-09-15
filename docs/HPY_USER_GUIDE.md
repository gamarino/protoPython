# Loading HPy-Style Extension Modules

> **Status (2026-09-15).** `HPyModuleProvider` is implemented and compiled into
> libprotoPython, but `PythonEnvironment` does not register it in the module resolution
> chain. `import` therefore does not load extension modules through it at present. This
> page describes the loader as implemented. The extension API itself is described in
> [HPY_DEVELOPER_GUIDE.md](HPY_DEVELOPER_GUIDE.md).

## Module resolution today

`PythonEnvironment` registers three module providers with protoCore's provider
registry:

- `NativeModuleProvider`: modules implemented in C++ in `src/library/`;
- `PythonModuleProvider`: `.py` source modules;
- `CompiledModuleProvider`: shared libraries produced by `protopyc`, which export
  `proto_module_init` (see [PROTOPYC_SPECIFICATION.md](PROTOPYC_SPECIFICATION.md)).

The directories they search come from the command line and the environment; see
[USER_GUIDE.md](USER_GUIDE.md#module-search-path).

## What `HPyModuleProvider` does

For a module name, `HPyModuleProvider` (`src/library/HPyModuleProvider.cpp`):

1. replaces dots in the name with `/` and, in each search directory, looks for
   `<name>.hpy.so` and then `<name>.so`;
2. opens the file with `dlopen(path, RTLD_NOW | RTLD_GLOBAL)`;
3. looks up the symbol `HPyInit_<name>` with `dlsym`, where `<name>` is the last
   component of a dotted module name;
4. calls it with a temporary `HPyContext` created for the current `ProtoContext`;
5. sets `__file__`, `__name__` and `__loader__` on the returned module object.

If any step fails the provider returns no module and prints no diagnostics. The
`PROTO_HPY_DEBUG` variable does not change this: it is compiled out of Release builds,
and even in diagnostic builds the loader emits no messages.

## Troubleshooting a module that fails to load

- The file name must match the init function: `foo.hpy.so` or `foo.so` must export
  `HPyInit_foo`.
- In C++ sources, declare the init function `extern "C"` so that its name is not
  mangled.
- Check for missing shared-library dependencies with `ldd`.
