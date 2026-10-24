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
   code --install-extension protopy-debug-0.2.0.vsix
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
- **Exception breakpoints** (the Breakpoints view's checkboxes; see below).
- **Threads**: every `threading.Thread` (and `_thread.start_new_thread`
  thread) is debugged, `concurrent.futures` workers included (see below).

## Exception breakpoints

Two filters, as the Python Debugger (debugpy) offers them:

| Filter | Default | Stops |
|--------|---------|-------|
| **Raised Exceptions** (`raised`) | off | where any exception is raised in your code, or where one raised by a library or by the runtime reaches your code — even when a `try` handles it afterwards. |
| **Uncaught Exceptions** (`userUnhandled`) | on | where an exception is raised when no `try`/`except` of your code on that thread would handle it: the walk goes outwards over the active `try` blocks of your frames, testing each `except` clause's type (a tuple of types, an attribute such as `mod.Error`, a local variable), up to the first frame of library code. An exception escaping a thread's target into `threading` stops too. |

The thread stops at the frame the exception is in, *before* anything handles
it: the call stack, the locals and the Debug Console show the state at the
raise. VS Code shows the exception's type and message (the Exception widget;
`exceptionInfo` also gives a traceback of the stopped stack). An error raised by
the runtime itself (`1 / 0`, `{}[k]`, a `TypeError` of an operator) or inside a
library stops in your frame that made the call, and the description says where
it was raised (`raised in native code`, `raised in json.loads (decoder.py:355)`).
Continuing lets the exception go on: to its handler, or to the end of the
program.

*Your code* is every file outside the standard library, `site-packages` and
`dist-packages`. Library code never stops for exceptions (line breakpoints in it
still work).

Never a stop, under either filter: `StopIteration`, `StopAsyncIteration` and
`GeneratorExit` (the iteration protocol, also when your `__next__` raises
`StopIteration`), `SystemExit` with code `None` or 0, exceptions the runtime or a
built-in raises and handles itself (the end of a `for` loop, `next(it, default)`,
`getattr(obj, name, default)`, `hasattr`, ...), exceptions inside library code
that library code handles, and exceptions raised by expressions the debugger
evaluates (watches, hovers, the Debug Console). An exception is reported once
as it propagates; a bare `raise` re-raising it does not stop again, `raise e`
does.

## Conditions, hit counts and logpoints

Right-click a breakpoint (or the gutter) → **Edit Breakpoint…** / **Add
Conditional Breakpoint…** / **Add Logpoint…**:

- **Expression** (`condition`): a Python expression evaluated in the frame at
  the line (its locals, then its globals and builtins); the thread stops when
  its value is true (`bool()`).
- **Hit Count** (`hitCondition`), counted per breakpoint over the hits whose
  condition passed (all threads together):

  | Hit count | Stops on hit |
  |-----------|--------------|
  | `N` or `== N` | the N-th only |
  | `>= N`, `> N` | from the N-th / after the N-th on |
  | `<= N`, `< N` | up to the N-th / before the N-th |
  | `% N` | every N-th (N, 2N, ...) |

  Anything else is refused: the breakpoint shows as unverified, with the
  accepted forms in its message, and is not installed.
- **Log Message** (`logMessage`): never stops; writes the message to the Debug
  Console, each `{expression}` replaced by its value in the frame (`str()` of a
  string, `repr()` of anything else; `<error: NameError: ...>` when it raises).
  `{{` and `}}` write literal braces. With a condition or a hit count it logs
  only the hits that pass them.

**Conditions on the exception filters.** Each filter of the Breakpoints view
has an edit button (**Edit Condition**): a Python expression evaluated in the
frame the exception is in, with the exception bound as **`exc`** (it shadows a
variable `exc` of that frame, for the condition only). Examples:
`isinstance(exc, KeyError)`, `not isinstance(exc, (KeyboardInterrupt, OSError))`,
`"timeout" in str(exc)`, `n > 3` (a local of the raising frame). *Raised
Exceptions* is tested first; when its condition is false, *Uncaught
Exceptions* (if enabled) is tested with its own condition.

**A condition that fails** (a syntax error, or it raises) **stops** the thread,
as if it were true, and says why: the stop's description and a line on the
Debug Console's stderr read ``breakpoint condition `...` failed: ZeroDivisionError:
division by zero`` (``exception breakpoint condition `...` failed: ...`` for a
filter). A condition is compiled once per text and kept; its syntax error is
reported the first time it is evaluated (the protocol thread cannot run Python
code, so not when it is set).

Conditions, log messages, watches, hovers and the Debug Console run on the
thread that hit the breakpoint, with the debugger switched off for that thread
while they run: a function they call does not stop at its breakpoints, and an
exception they raise is not an exception stop.

## Threads

Each Python thread appears in the Call Stack view with its `threading` name
(`MainThread`, `Thread-1 (worker)`, `ThreadPoolExecutor-0_0`; the name is read
again at each stop, so a renamed thread shows its new name). A thread started
with `_thread.start_new_thread` shows its function's name.

**Only the thread that stops is stopped.** A breakpoint, a step or an exception
holds that thread; the others keep running (and printing). Several threads can
be stopped at once, each with its own call stack, variables and Debug Console
frame. Continue, Step Over/Into/Out and Pause act on the thread selected in the
Call Stack view: stepping one thread never stops another (a breakpoint still
does). Pause on a thread blocked in native code (`time.sleep`, a lock, I/O)
takes effect when it next runs Python code. Breakpoints and exception filters
apply to every thread. Stopping the session releases every stopped thread.

The collector keeps running while threads are stopped: a stopped thread waits
outside protoCore's stop-the-world quorum, and its values stay rooted.

## Limitations

- **No function or data breakpoints.** No "stop all threads" mode: a stop
  holds one thread.
- **Exception filters are approximations of CPython's handler search.** A
  `with` block counts as handling an exception only for `contextlib.suppress`
  (another context manager's `__exit__` is not called to find out, so
  *Uncaught Exceptions* may stop on an exception such a manager would
  suppress). Native code between two Python frames is not seen: an
  `AttributeError` raised by your property and caught by `hasattr()` stops
  under *Uncaught Exceptions* unless a `try` of yours encloses the `hasattr`.
  An `except` clause whose type is computed by anything but names, attributes
  and tuples (a call, a subscript) is taken as matching.
- **Threads not started by Python code are not debugged** (threads a native
  module starts itself). A thread blocked in native code is listed, but shows
  no call stack until it stops (pause it: it stops when it returns to Python).
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
thread and of every Python thread it starts (`debugger::ThreadScope` in the
thread bootstrap of `src/library/ThreadModule.cpp`)
([`include/protoPython/Debugger.h`](../include/protoPython/Debugger.h),
`Activation::debugInstruction` in `src/library/ExecutionEngine.cpp`); the
adapter (`src/library/DAPServer.cpp`) decides whether to stop. Each thread has
its own state in the adapter (step mode, pause request, the stop's frames, a
queue of inspection tasks); frame and variable ids are unique across threads. A
stopped thread waits outside protoCore's collector quorum (an unmanaged region)
and wakes to run inspection requests itself, so variables and evaluation always
touch objects on the thread that owns them; values the client can expand stay
pinned in a root set until that thread resumes. The protocol reader is a plain
thread that never touches a Python object.

Exception events come from the two places where a Python exception crosses an
activation: the C++ `catch` of a protected range (`Activation::runRange`) and
the one of the whole activation (`executeBytecodeRange`). The first frame of the
user's code that sees an exception reports it, while that frame and all its
callers still exist (their operand stacks, locals and try blocks intact). For
*Uncaught Exceptions* the adapter then asks whether a handler would take it:
`Activation::debugRangeHandles` reads the handler code of each active try block
(the layout `Compiler::compileTry` emits), evaluates each `except` clause's type
expression in its frame and tests it as `EXCEPTION_MATCH` does. With no filter
on, an exception costs one test of a flag.

Requests: `initialize`, `launch`, `setBreakpoints` (with `condition`,
`hitCondition`, `logMessage`), `setExceptionBreakpoints` (filters `raised`,
`userUnhandled`, and `filterOptions` with a `condition`), `exceptionInfo`, `configurationDone`,
`threads`, `stackTrace`, `scopes`, `variables`, `evaluate`, `continue`, `next`,
`stepIn`, `stepOut`, `pause`, `disconnect`, `terminate`. Events: `initialized`,
`stopped` (`allThreadsStopped: false`), `thread` (`started`, `exited`),
`output`, `exited`, `terminated`.

## Testing without VS Code

`test/regression/dap_session.py` drives `protopy --dap` as VS Code does
(Windows paths with a lower-case drive letter included) and checks
breakpoints, stepping, the stack, variables, evaluation, `stopOnEntry`, more
than 64 KiB of non-ASCII output, stdin at end of file, an uncaught exception,
both exception filters (and no stop on internal control flow), two threads at
one breakpoint, pausing one busy thread, collections while a thread is held,
conditions, hit counts and logpoints, and conditions on the exception filters.
CTest runs it as `protopy_dap_session`:

```bash
python3 test/regression/dap_session.py build/bin/protopy test/regression
```
