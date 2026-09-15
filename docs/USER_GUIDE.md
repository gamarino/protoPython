# protoPython User Guide

protoPython is a Python 3.14 runtime built on
[protoCore](https://github.com/numaes/protoCore). It has no Global Interpreter Lock:
Python threads are native OS threads that run in parallel. This guide covers the
`protopy` command-line interface and everyday use.

## Status

protoPython 1.0.0 is not production ready.
[CPYTHON_CONFORMANCE.md](CPYTHON_CONFORMANCE.md) records the current conformance
status and the known divergences from CPython;
[PYTHON_COMPATIBILITY.md](PYTHON_COMPATIBILITY.md) summarises supported language
features and the main differences.

## Running code

The runtime executable is `protopy`. In a build tree it is
`build_release/src/runtime/protopy`; see [INSTALLATION.md](INSTALLATION.md).

| Command | Effect |
|---------|--------|
| `protopy script.py [args...]` | Run a script as `__main__`. A positional argument that contains `/` or ends in `.py` is treated as a script path. |
| `protopy --script script.py [args...]` | Same, with an explicit flag. |
| `protopy name [args...]`, `protopy -m name`, `protopy --module name` | Find module `name` on the search path and run it as `__main__`. |
| `protopy -c "code" [args...]` | Run the code given as a string. |
| `protopy -i`, `protopy --repl` | Start the interactive REPL. |
| `protopy -h`, `protopy --help` | Print the usage text and exit with status 0. |

Only one of a script, a module and `-c` may be given. Arguments that follow the target
are passed to the program in `sys.argv`; `sys.argv[0]` is the script path, the module
name or `-c`.

Running `protopy` without arguments prints the usage text and exits with status 64. It
does not start the REPL; use `-i`.

A script's module body runs once as `__main__`, as in CPython. protopy never calls a
`main()` function on its own; call it explicitly, typically under
`if __name__ == "__main__":`.

### Options

| Option | Effect |
|--------|--------|
| `-p <dir>`, `--path <dir>` | Add a module search directory (repeatable). |
| `--stdlib <dir>` | Use `<dir>` as the standard library directory. |
| `--dry-run` | For a script or module target: check that the script file exists, or that the module's `.py` file or package directory is in the standard library or a search directory, then exit with status 0 or 65 without running anything. Modules implemented natively in C++ are not found by this check. |
| `--bytecode-only` | Currently performs the same check as `--dry-run`; nothing is executed. |
| `--trace` | Print module enter and leave events to standard error. |

For compatibility with tools that start Python subprocesses (for example
`test.support.script_helper`), protopy accepts and ignores the CPython flags `-I`,
`-E`, `-S`, `-s`, `-O`, `-OO`, `-B`, `-q`, `-u`, `-b`, `-bb`, `-d`, `-v`, `-vv` and
`-vvv`, and `-X <opt>` and `-W <opt>` (also written `-X<opt>` and `-W<opt>`). Any other
argument that starts with `-` is a usage error.

### Exit status

| Status | Meaning |
|--------|---------|
| 0 | Success. |
| 64 | Usage error: unknown option, missing option value, more than one target, or no target at all. |
| 65 | The script file or module could not be found. |
| 70 | The program ended with an unhandled exception; the traceback is printed to standard error. |
| *n* | The program raised `SystemExit(n)` or called `sys.exit(n)` with an integer *n*. This applies to scripts, modules and `-c`. |

As in CPython, `SystemExit` without an argument or with `None` exits with status 0, and
`SystemExit` with any other object prints that object to standard error and exits with
status 1. The REPL exits with status 0 at the end of its input.

## Module search path

protopy passes these directories to the runtime:

- the directory of the script (for a script) or the current directory (for `-c` and the REPL);
- directories given with `-p`/`--path`;
- directories listed in the `PROTO_PYTHONPATH` environment variable, separated by `:`;
- on Linux, `~/.local/lib/python3.14/site-packages` (not for the REPL).

`PYTHONPATH` is not read. When `PROTO_PYTHONPATH` is not set, protopy sets it to `1` in
its own environment so that Python code can detect that it runs under protoPython.

The standard library directory is taken from `--stdlib`; otherwise from the path
compiled into the binary; otherwise protopy looks for `lib/python3.14` in the
executable's directory and its parent directories, and finally in the current
directory.

## Interactive REPL

`protopy -i` reads statements from standard input, executes them and prints the value
of expression statements.

Known limitation: with input piped to `protopy -i`, a name bound on one line is not
visible on later lines. For example, `x = 5` followed by `print(x)` raises `NameError`.

## Language compatibility

protoPython targets the Python 3.14 language: control flow, functions with default,
`*args` and `**kwargs` parameters, classes, generators (`yield`, `yield from`),
`async`/`await`, comprehensions and slicing. See
[PYTHON_COMPATIBILITY.md](PYTHON_COMPATIBILITY.md) for details and
[CPYTHON_CONFORMANCE.md](CPYTHON_CONFORMANCE.md#known-divergences-pending) for the
known divergences.

## Threads without a GIL

The standard `threading` module runs code in parallel:

```python
import threading

def worker():
    print("Working in thread:", threading.current_thread().name)

threads = []
for i in range(4):
    t = threading.Thread(target=worker)
    threads.append(t)
    t.start()

for t in threads:
    t.join()
```

Under protopy these threads run on separate CPU cores at the same time. protoCore's
collections are immutable and share structure between versions, and updates to mutable
objects are applied without a global lock. A sequence of operations that must not
interleave with other threads still needs a `threading.Lock`.

Prefer immutable values (tuples, strings) for data shared between threads.

## Troubleshooting

- **Module not found**: add its directory with `--path` or `PROTO_PYTHONPATH`
  (`PYTHONPATH` is ignored).
- **Standard library not found** (for example `ModuleNotFoundError` for `json`): pass
  `--stdlib <dir>` pointing at the `python3.14` library directory.
- **Recursion depth**: `sys.setrecursionlimit` and `sys.getrecursionlimit` are
  available.
- **Internal diagnostics**: `PROTO_ENV_DIAG=1` prints diagnostic output only in
  builds compiled without `NDEBUG` or with `-DPROTOPY_DIAG_ENABLED=1`. In Release
  builds the diagnostics are compiled out and the variable has no effect.
