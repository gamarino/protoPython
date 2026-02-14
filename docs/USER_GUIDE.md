# ProtoPython User Guide

Welcome to **ProtoPython**, a high-performance, GIL-free Python 3.14 compatible runtime. This guide provides instructions on how to use the ProtoPython environment effectively.

## Introduction

ProtoPython is designed for elite performance and true parallel concurrency. Unlike standard CPython, ProtoPython does not have a Global Interpreter Lock (GIL), allowing OS threads to execute Python code in parallel across all available CPU cores.

## CLI Usage

The primary entry point is the `protopy` executable.

### Running a Script
To run a Python script:
```bash
protopy myscript.py
```

### Interactive REPL
To enter the interactive Read-Eval-Print Loop (REPL):
```bash
protopy
```
The REPL supports standard Python syntax, multi-line input, and improved diagnostics.

### Command Line Flags
- `-c cmd`: Run the Python code provided as a string.
- `-m module`: Run a library module as a script.
- `-h`: Show the help message and exit.

## Language Compatibility

ProtoPython targets compatibility with **Python 3.14**. Supported features include:
- All core control flow (`if`, `for`, `while`, `try`, `except`).
- Advanced syntax: `async`/`await`, generators (`yield` and `yield from`).
- Fully balanced collections (`list`, `dict`, `set`, `tuple`) using AVL trees for $O(\log N)$ stability.
- Slicing and negative indexing.
- Variable arguments (`*args`, `**kwargs`).

For a detailed list of compatibility and differences from CPython, see [Python Compatibility Guide](PYTHON_COMPATIBILITY.md).

## Concurrency and GIL-Free Execution

The most significant advantage of ProtoPython is its **GIL-free architecture**.

### Using Threads
You can use the standard `threading` module to run code in parallel.
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
In ProtoPython, these threads run truly in parallel on multiple CPU cores.

### Thread-Safe Multimutability
ProtoPython uses a revolutionary "multimutable" model. While many core objects are immutable and utilize structural sharing, the runtime ensures that shared state remains safe without the overhead of heavy locking.

## Best Practices

1. **Leverage Immutability**: Prefer tuples and strings for shared data to maximize structural sharing efficiency.
2. **Parallelize CPU-Bound Tasks**: Take advantage of the GIL-free runtime by using threads for CPU-intensive work.
3. **Use HPy Extensions**: For performance-critical C extensions, use HPy to ensure compatibility with the GIL-free model.

## Troubleshooting

- **Module Not Found**: Ensure your `PYTHONPATH` or `site-packages` directory is correctly configured.
- **Recursive Overflow**: ProtoPython has a configurable recursion limit (`sys.setrecursionlimit`).
- **Performance Analysis**: Use `PROTO_ENV_DIAG=1` to enable diagnostic output for analysis.
