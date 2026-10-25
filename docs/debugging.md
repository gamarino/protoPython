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
   code --install-extension protopy-debug-0.3.1.vsix
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
| `pathMappings` | `[{"localRoot": ..., "remoteRoot": ...}]`: folders this machine and protopy name differently (see [Remote debugging](#remote-debugging)). |
| `remote`      | VS Code extension only: `{"command": [...], "path": "protopy"}` runs the adapter through ssh, wsl, docker exec, ... |

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

## Sources without a file

Every frame, breakpoint and loaded source names a file the client can open by
its `path`. Code that has no file the client can open comes with a
`sourceReference` instead, and VS Code shows its text (served by the adapter,
read-only) when a frame or a breakpoint is in it:

- code compiled from a string: `exec(src)`, `eval(src)`, `compile(src,
  "<generated>", "exec")` and the functions it defines (the name is the pseudo
  file name, `<string>` by default); the text is kept while the session runs;
- with `pathMappings`, a file under none of the mappings (the standard library
  of the remote side, a module outside the mapped folders): its `name` is the
  file's name and its `origin` the adapter's path.

Breakpoints can be set in those read-only editors: they are sent by
reference and hit like any other. Standard library and site-packages code is
marked `deemphasize` (VS Code greys its frames). The **Loaded Scripts** view
(Run and Debug → ⋯ → Loaded Scripts) lists every source the program ran: the
script, the modules it imported (standard library included) and code compiled
from strings, as they load.

protoPython has no frozen or embedded Python sources: the standard library is
files on disk, so stepping into it shows its file (or, remotely, its text by
reference). Modules compiled with `protopyc` and native modules still have no
bytecode and are not seen.

## Remote debugging

**Option 1: the editor runs remotely** (VS Code Remote - SSH, WSL, Dev
Containers). Install protopy and the extension's `.vsix` on the remote side
(`code --install-extension` from a remote terminal, or Extensions → *Install
in SSH: host*); the extension declares `extensionKind: workspace`, so it runs
there, next to protopy. Paths are the same on both sides: nothing else to set.

**Option 2: only the adapter runs remotely.** The local VS Code starts
`protopy --dap` through a command whose stdin/stdout carry the protocol, and
`pathMappings` tells the adapter how the two sides name the project:

```json
{
  "type": "protopy", "request": "launch", "name": "protopy: over SSH",
  "remote": { "command": ["ssh", "-T", "-o", "BatchMode=yes", "me@build-box"],
              "path": "/home/me/.local/bin/protopy" },
  "pathMappings": [{ "localRoot": "${workspaceFolder}", "remoteRoot": "/home/me/project" }],
  "program": "${file}",
  "cwd": "${workspaceFolder}"
}
```

```json
{
  "type": "protopy", "request": "launch", "name": "protopy: in WSL",
  "remote": { "command": ["wsl", "-d", "Ubuntu-24.04", "--"],
              "path": "/home/me/.local/bin/protopy" },
  "pathMappings": [{ "localRoot": "${workspaceFolder}", "remoteRoot": "/home/me/project" }],
  "program": "${file}"
}
```

(`docker exec -i <container>` works the same way.) The extension runs
`<command...> <path> --dap`; the rest is done by the adapter, so any DAP client
can use the same attributes:

- `program`, `cwd`, breakpoint paths and `source` requests are translated from
  the client's names to the adapter's; stack frames, breakpoints, loaded sources
  and the exception traceback back to the client's. A path already on the
  adapter's side (`/home/me/project/x.py`) is used as is.
- The longest matching `localRoot` / `remoteRoot` wins; `\` and `/` are
  converted both ways; a root that looks like a Windows path (`c:\...`,
  backslashes) matches regardless of case, so VS Code's `c:` and `C:` both work.
- A file under no mapping is shown by reference (see above), not by a path
  the client could not open.
- A mapping without both roots is a launch error; `remote` without
  `pathMappings` is refused by the extension with an explanation.

The command must give a clean byte stream: the remote shell must print nothing
on a non-interactive start (no `echo` or motd in `.bashrc` / `.profile` for
non-interactive shells), and ssh must not ask anything (`-T`, `-o
BatchMode=yes`, a key in the agent). The script's own output is carried inside
the protocol, so it is not a problem.

## JetBrains IDEs (IntelliJ IDEA, PyCharm, CLion, ...)

*This section was written from the LSP4IJ plugin's documentation and the
adapter's protocol behaviour; it has not been clicked through in a JetBrains
IDE, and menu names may differ between versions.*

JetBrains IDEs debug through Debug Adapter Protocol servers with Red Hat's
[LSP4IJ](https://github.com/redhat-developer/lsp4ij) plugin (Settings →
Plugins → Marketplace → LSP4IJ):

1. Run → Edit Configurations… → **+** → **Debug Adapter Protocol**.
2. *Server* tab: a new DAP server whose command is `protopy --dap` (the full
   path, e.g. `%LOCALAPPDATA%\Programs\proto\bin\protopy.exe --dap`; remotely,
   the ssh form `ssh -T -o BatchMode=yes me@host /home/me/.local/bin/protopy
   --dap`), launched by the IDE (not connected to by socket).
3. *Mappings* tab: the files it debugs, so breakpoints can be set in them —
   file name pattern `*.py` (or the Python language / file type).
4. *Configuration* tab: the working directory, the file to debug, *launch*
   mode, and the launch parameters as JSON — the same attributes as in a VS Code
   launch.json:

   ```json
   { "type": "protopy", "request": "launch", "program": "${file}",
     "cwd": "${workspaceFolder}", "stopOnEntry": false,
     "pathMappings": [{ "localRoot": "...", "remoteRoot": "..." }] }
   ```

   (replace the `${...}` variables with what the plugin offers, or with
   absolute paths).

In **PyCharm** (and IntelliJ with the Python plugin) this configuration
coexists with PyCharm's own Python debugger: a *Python* run configuration still
debugs on CPython with PyCharm's debugger, a *Debug Adapter Protocol* one on
protoPython. Python files keep PyCharm's highlighting and code insight, so no
TextMate bundle is needed. The exception filters appear after a first run (the
adapter sends them at `initialize`); per LSP4IJ's documentation, Pause,
Loaded Scripts and exceptionInfo are not supported by the plugin itself, while
breakpoints (with conditions), stepping, variables, evaluation and `source`
references are.

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
- **`launch` only**: no attaching to a running `protopy`. Remote debugging
  starts protopy through a command (`remote`); it does not attach to one.
- **Texts of code compiled from strings are kept for the session** (with their
  code objects): a program that compiles very many distinct strings keeps them
  all while it is debugged.
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

Requests: `initialize`, `launch` (with `pathMappings`), `setBreakpoints` (with
`condition`, `hitCondition`, `logMessage`; by `path` or `sourceReference`),
`source`, `loadedSources`, `setExceptionBreakpoints` (filters `raised`,
`userUnhandled`, and `filterOptions` with a `condition`), `exceptionInfo`, `configurationDone`,
`threads`, `stackTrace`, `scopes`, `variables`, `evaluate`, `continue`, `next`,
`stepIn`, `stepOut`, `pause`, `disconnect`, `terminate`. Events: `initialized`,
`stopped` (`allThreadsStopped: false`), `thread` (`started`, `exited`),
`loadedSource` (`new`),
`output`, `exited`, `terminated`.

## Testing without VS Code

`test/regression/dap_session.py` drives `protopy --dap` as VS Code does
(Windows paths with a lower-case drive letter included) and checks
breakpoints, stepping, the stack, variables, evaluation, `stopOnEntry`, more
than 64 KiB of non-ASCII output, stdin at end of file, an uncaught exception,
both exception filters (and no stop on internal control flow), two threads at
one breakpoint, pausing one busy thread, collections while a thread is held,
conditions, hit counts and logpoints, conditions on the exception filters, and
a session through `pathMappings` with a fake client root (client paths, sources
by reference, loaded sources, breakpoints on references).
CTest runs it as `protopy_dap_session`:

```bash
python3 test/regression/dap_session.py build/bin/protopy test/regression
```
