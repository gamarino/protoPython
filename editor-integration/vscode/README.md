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
code --install-extension protopy-debug-0.3.0.vsix
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
