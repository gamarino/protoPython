# HPy + REPL Integration

This document outlines how HPy modules interact with the `protopy` REPL and the underlying `protoCore` execution engine.

## Overview

The `protopy` REPL supports loading HPy modules seamlessly. When an HPy module is imported in the REPL, it is registered in `sys.modules` just like any native or Python module.

## Core Mechanics

### 1. HPyContext Management
A single `HPyContext` is maintained within the `PythonEnvironment`. All HPy modules loaded in the same environment share this context, ensuring handle consistency.

### 2. Handle Lifetime in REPL
In the REPL, handles are created for every object returned to or passed from an HPy module.
- **Persistent Handles**: Handles for global objects (e.g., those assigned to variables in the REPL) remain valid for the duration of the session.
- **Transient Handles**: Handles created during a single REPL command execution are closed when the command finishes, preventing handle leaks.

### 3. Error Handling
Exceptions raised within HPy modules are translated into `protoCore` exceptions, which are then caught and formatted by the REPL's diagnostic engine.
- If an HPy module uses `HPyErr_SetString`, it creates a standard Python-level exception object.
- The `%debug` magic in the REPL can inspect the state after an HPy-originated exception.

## Best Practices for HPy in REPL

- **Handle Diligence**: While the REPL attempts to manage transient handles, HPy modules should always use `HPy_Close` for handles they create and no longer need.
- **Reloading**: Currently, reloading an HPy module requires restarting the REPL, as the underlying `.so` handles are not explicitly tracked for unloading.

## Diagnostic Integration

HPy objects fully participate in the `dir()` and `help()` builtins.
- `dir(hpy_obj)` will list all methods and members defined in the `HPyModuleDef`.
- `help(hpy_obj)` will display docstrings if provided in the HPy definition.
