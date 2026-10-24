# Debugging Python scripts on protoPython in VS Code

`protopy` has a [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
(DAP) server built in. With the small extension in
[`editor-integration/vscode/`](../editor-integration/vscode/) VS Code debugs
Python scripts *running on protoPython*: line breakpoints, stepping, the call
stack, variables, watches and the Debug Console.

It is a separate debugger from the Python Debugger (debugpy) extension, which
debugs scripts on CPython. Both can be installed: Python files keep VS Code's
Python grammar and language features, and each launch configuration says which
runtime it uses (`"type": "protopy"` or `"type": "debugpy"`).

## Install

1. Build and install protoPython ([INSTALLATION.md](INSTALLATION.md)). On
   Windows the default install puts `protopy.exe` in
   `%LOCALAPPDATA%\Programs\proto\bin`; elsewhere put `protopy` in
   `~/.local/bin` or on your `PATH`.
2. Package and install the extension (needs Node.js for `npx`):

   ```bash
   cd editor-integration/vscode
   npx @vscode/vsce@3.2.1 package --skip-license
   code --install-extension protopy-debug-0.1.0.vsix
   ```

The extension runs the `protopy` named by the setting **protopy.path**
(Settings → Extensions → protoPython); if it is empty, the default install
location above when it exists; otherwise `protopy` from `PATH`.

## Start a session

- **With a launch.json**: Run and Debug → **Add Configuration…** →
  **protoPython**, or copy [`examples/.vscode/launch.json`](../examples/.vscode/launch.json)
  (open the `examples/` folder to use it as is). Select the configuration and
  press **F5**.
- **Without one**: open a `.py` file, press **F5** and choose
  **protoPython (protopy)** in the list of debuggers VS Code offers. The
  extension never answers for another debugger: with the Python Debugger
  extension installed, F5 without a configuration still asks.

```json
{
  "type": "protopy",
  "request": "launch",
  "name": "protopy: current file",
  "program": "${file}",
  "cwd": "${fileDirname}",
  "args": [],
  "env": {},
  "stopOnEntry": false
}
```

| Attribute     | Meaning |
|---------------|---------|
| `program`     | The script (required). It runs as `protopy <program> <args>` would run it: same `sys.path`, `sys.argv`, `__main__`, exit status. |
| `args`        | `sys.argv[1:]`. |
| `cwd`         | The script's working directory. |
| `env`         | Environment variables set for the script. |
| `stopOnEntry` | Stop at the first line of the script. |

## What works

- **Line breakpoints** in any Python file the program runs (the script, its
  modules, the standard library). A breakpoint on a blank or comment line moves
  to the next line with code, and VS Code shows it there.
- **Continue, Step Over, Step Into, Step Out, Pause.** Step Over runs calls to
  completion (stopping at a breakpoint inside them); Step Out stops in the
  caller right after the call returns.
- **Call stack**: every Python frame with its function name (`co_qualname`),
  file and current line; selecting a frame shows its variables and evaluates
  in it.
- **Variables**: *Locals* (the function's variables, or the module's at module
  level) and *Globals* (the module's), with `repr()` values and types. Lists,
  tuples, dicts, sets, modules and objects with a `__dict__` expand (the first
  2000 entries; a `repr()` longer than 4000 characters is cut).
- **Watch, hover and Debug Console**: any expression, evaluated with `eval()`
  in the selected frame. In the Debug Console a statement is run with `exec()`
  (`print(x)`, `import os`, an assignment at module level).
- **Output**: what the script writes to stdout and stderr appears in the Debug
  Console, in order, including an uncaught exception's traceback. The exit
  status of the script is the session's.

## Limitations

- **One thread.** Only the thread that runs the script is debugged. Threads it
  starts run freely: their breakpoints are not hit and they are not listed.
- **No conditional breakpoints, logpoints, hit counts, function or exception
  breakpoints.** An uncaught exception ends the program (its traceback is
  shown) instead of stopping at the raise.
- **`launch` only**: no attaching to a running `protopy`.
- **Standard input reads end of file.** The adapter's stdin carries the
  protocol, so a script reading `sys.stdin` or calling `input()` gets EOF.
- **Locals are a snapshot.** Assigning to a function's local variable from the
  Debug Console does not change the running function (module-level assignments
  do).
- **Lines without code of their own** (`else:`, `try:`, the inner lines of a
  multi-line string) never stop. Each line of a statement split over several
  lines can stop while stepping.
- **Code without bytecode is invisible**: native modules, built-in functions and
  modules compiled with `protopyc` run without stops.
- While a session is active every bytecode instruction pays a check of a few
  instructions; outside `--dap` the interpreter does no extra work.

## How it works

`protopy --dap` reads Content-Length framed DAP requests on stdin and writes
responses and events on stdout. Before the program starts it takes private
copies of the process's standard descriptors for the protocol, points stdout and
stderr at pipes whose bytes become `output` events, and stdin at the null
device: the script's prints can never corrupt the protocol stream.

The script runs on the main thread once the client sends `configurationDone`.
The bytecode dispatcher reports a *line event* for each new source line of that
thread ([`include/protoPython/Debugger.h`](../include/protoPython/Debugger.h),
`Activation::debugInstruction` in `src/library/ExecutionEngine.cpp`); the
adapter (`src/library/DAPServer.cpp`) decides whether to stop. A stopped thread
waits outside protoCore's collector quorum (an unmanaged region) and wakes to
run inspection requests itself, so variables and evaluation always touch
objects on the thread that owns them; values the client can expand stay pinned
in a root set until the program resumes. The protocol reader is a plain thread
that never touches a Python object.

Requests: `initialize`, `launch`, `setBreakpoints`, `setExceptionBreakpoints`
(accepted, no filters), `configurationDone`, `threads`, `stackTrace`, `scopes`,
`variables`, `evaluate`, `continue`, `next`, `stepIn`, `stepOut`, `pause`,
`disconnect`, `terminate`. Events: `initialized`, `stopped`, `output`,
`exited`, `terminated`.

## Testing without VS Code

`test/regression/dap_session.py` drives `protopy --dap` as VS Code does
(Windows paths with a lower-case drive letter included) and checks
breakpoints, stepping, the stack, variables, evaluation, `stopOnEntry`, more
than 64 KiB of non-ASCII output, stdin at end of file and an uncaught exception.
CTest runs it as `protopy_dap_session`:

```bash
python3 test/regression/dap_session.py build/bin/protopy test/regression
```
