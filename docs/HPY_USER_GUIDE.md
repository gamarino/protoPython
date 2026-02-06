# HPy User Guide: Module Loading in protoPython

This guide explains how to load and use HPy extension modules in `protoPython`.

## Module Loading Flow

When you execute `import my_module`, `protoPython` follows this resolution sequence:

1.  **Built-in Modules**: Checks if it's a hardcoded native module.
2.  **HPy Extensions**: Searches for `my_module.hpy.so` or `my_module.so` in the search paths.
3.  **Python Modules**: Searches for `my_module.py`.

## Configuring Search Paths

`protoPython` searches for modules in the directories specified during initialization.

### From CLI
```bash
./protoPython -p ./extensions my_script.py
```

### From Python (sys.path)
You can append paths to `sys.path` (if the `sys` module is fully integrated) or ensure they are present in the environment's search paths at startup.

## Internal Mechanism

The `HPyModuleProvider` handles the heavy lifting:
1.  **Locate**: Finds the shared library on disk.
2.  **Load**: Uses `dlopen(path, RTLD_NOW | RTLD_GLOBAL)`.
3.  **Resolve**: Finds the symbol `HPyInit_<name>` using `dlsym`.
4.  **Init**: Calls the init function, passing an `HPyContext` initialized for the current `ProtoContext`.
5.  **Wire**: Injects the resulting module object into the internal module map and sets metadata like `__file__` and `__name__`.

## Troubleshooting

### `HPyModuleProvider: Failed to load ...`
- **Missing File**: Ensure the `.so` is in a recognized search path.
- **Dependencies**: The `.so` might be missing a system dependency (check with `ldd`).
- **Permissions**: Ensure the file is readable and executable.

### `HPyModuleProvider: Failed to find symbol HPyInit_...`
- **Naming Mismatch**: The filename must match the suffix of the `HPyInit_` function.
- **C++ Mangling**: If writing in C++, ensure the init function is wrapped in `extern "C"`.

### Debugging
Run `protoPython` with the environment variable `PROTO_HPY_DEBUG=1` to see detailed loading logs.

```bash
PROTO_HPY_DEBUG=1 ./protoPython my_script.py
```
