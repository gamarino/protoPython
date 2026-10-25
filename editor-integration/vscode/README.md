# protoPython Debugger — VS Code extension

A minimal VS Code extension that debugs Python scripts on **protoPython**
(`protopy`). The debug adapter is the `protopy` executable itself, run as
`protopy --dap`, which speaks the
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
over stdin/stdout.

It adds a debugger only. Python files keep VS Code's own Python grammar and
whatever the Python / Python Debugger extensions provide; this extension
registers the debug type `protopy` for the `python` language and never takes
over their F5 default. There is no TypeScript build step and no bundler.

## Requirements

`protopy` installed or built. The extension looks for it in this order:

1. the `protopy.path` setting (Settings → Extensions → protoPython), if set;
2. the default install location — `%LOCALAPPDATA%\Programs\proto\bin\protopy.exe`
   on Windows, `~/.local/bin/protopy` elsewhere — if it exists;
3. `protopy` on your `PATH`.

## Install

```bash
cd editor-integration/vscode
npx @vscode/vsce@3.2.1 package --skip-license
code --install-extension protopy-debug-0.3.1.vsix
```

On Linux and macOS you can instead symlink the folder (edits are picked up
after **Developer: Reload Window**):

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protopy-debug-0.2.0
```

## Use

- With a `launch.json`: add a configuration of type `protopy`
  (**Add Configuration…** → **protoPython**, or copy
  `examples/.vscode/launch.json`), select it in Run and Debug, press **F5**.
- Without one: open a `.py` file, press **F5**, and pick
  **protoPython (protopy)** when VS Code asks which debugger to use (if the
  Python Debugger extension is installed, VS Code lists both).

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

| Attribute     | Type     | Required | Meaning                                         |
|---------------|----------|----------|-------------------------------------------------|
| `program`     | string   | yes      | The script to run.                              |
| `args`        | string[] | no       | `sys.argv[1:]`.                                 |
| `cwd`         | string   | no       | Working directory of the script.                |
| `env`         | object   | no       | Environment variables for the script.           |
| `stopOnEntry` | boolean  | no       | Stop at the script's first line (default off).  |

Exception breakpoints come from the adapter: the Breakpoints view shows
**Raised Exceptions** (off) and **Uncaught Exceptions** (on) checkboxes. Every
Python thread of the program is listed in the Call Stack view under its
`threading` name; a stop holds only that thread, and Continue / Step / Pause
act on the thread selected there. Conditional breakpoints, hit counts,
logpoints and conditions on the exception filters (the exception is `exc`)
work as in the Python Debugger.

## Remote

- Run VS Code itself remotely (Remote - SSH, WSL, Dev Containers) and install
  protopy and this `.vsix` there: the extension is a `workspace` extension.
- Or keep VS Code local and start the adapter through a command:

  ```json
  "remote": { "command": ["ssh", "-T", "-o", "BatchMode=yes", "me@host"], "path": "protopy" },
  "pathMappings": [{ "localRoot": "${workspaceFolder}", "remoteRoot": "/home/me/project" }]
  ```

  The extension runs `<command...> <path> --dap`; `pathMappings` (required with
  `remote`) is applied by the adapter. Snippets: *Launch over SSH*, *Launch in
  WSL*. The remote shell must print nothing on a non-interactive start.

See `docs/debugging.md` in the repository for what works and the limitations.

## Testing the extension

`test/` holds integration tests that run this extension inside a real VS Code
(an Extension Development Host, through `@vscode/test-electron` and mocha)
and debug `test/fixture/fixture.py` with `protopy`, driving VS Code's own
debug API the way F5 does:

```bash
cd editor-integration/vscode
npm install
npm test
```

They check: a local launch (breakpoint hit on the right line, `stackTrace`,
a local's value, the program's stdout as `output` events, `terminated`); F5
without a `launch.json` on the open fixture; the executable lookup (a
`protopy.path` that does not exist is refused with a message naming it; with
the setting empty the default install is the process that runs); a `remote`
launch through WSL with `pathMappings` (the breakpoint is set by the Windows
path and the frames come back with Windows paths); a `remote` launch without
`pathMappings` refused with its explanation; the `raised` exception filter;
and that the launch schema, snippets and initial configurations agree.

- **Which VS Code.** `$VSCODE_EXECUTABLE` if set; on Windows the per-user
  install when present (unless it is halfway through an update, which makes
  every new instance refuse to start); otherwise a pinned VS Code is
  downloaded (`$VSCODE_VERSION` overrides the version) into
  `$VSCODE_TEST_CACHE`.
- **Isolation.** A throwaway `--user-data-dir` and `--extensions-dir` under
  `$PROTO_VSCODE_E2E_DIR` (default `C:\tmp\protowin\vscode-e2e` on Windows,
  `$TMPDIR/proto-vscode-e2e` elsewhere), recreated on every run, and every
  other extension disabled: your own VS Code may stay open.
- **Which runtime.** `$PROTO_E2E_EXE` (put into `protopy.path`), else the
  extension's own lookup: default install, then `PATH`. The default-install
  test is skipped when there is no default install.
- **Remote.** Runs only on Windows with WSL (`$PROTO_E2E_WSL_DISTRO`, default
  `Ubuntu-24.04`) and the Linux build named in `test/lang.js`; it is skipped
  elsewhere.
- `$PROTO_E2E_GREP` runs only the tests whose title matches.

`test/runTest.js`, `test/suite/` are the same in the five proto* runtimes'
extensions; `test/lang.js` and the fixture hold the per-language part.
`test/` and `node_modules/` are left out of the `.vsix` (`.vscodeignore`).
CI runs the local tests on Linux under `xvfb-run` (`.github/workflows/ci.yml`).
