# protopy Scope and Architecture Decision

## Decision: a bytecode executor inside protoPython

`protopy` is a bytecode executor implemented inside protoPython. It uses
`PythonEnvironment` and protoCore's `ProtoContext` directly. The alternative, forking
CPython 3.14 and replacing its evaluation loop, was not taken.

## Rationale

- **Code location**: all runtime code lives in this repository
  ([src/runtime](../src/runtime/) and [src/library](../src/library/)); there is no
  CPython fork to maintain or keep in sync.
- **Dependencies**: the build depends only on protoCore and the protoPython library,
  not on a CPython build tree. At run time protopy uses the pure-Python standard
  library in `lib/python3.14`.
- **No GIL to remove**: the executor is written for protoCore's concurrency model from
  the start, instead of removing the GIL from CPython's code base.

## Consequences

- **Frontend**: protoPython has its own tokenizer, parser and compiler
  (`include/protoPython/Tokenizer.h`, `Parser.h`, `Compiler.h`), which compile Python
  source into protoPython bytecode.
- **Bytecode format**: the opcode names follow CPython, but the numbering, argument
  encoding and semantics are protoPython's own, and some opcodes have no CPython
  counterpart (raw-list and fused opcodes). CPython bytecode files and tools such as
  `dis` do not apply. The opcodes are listed in
  [EXECUTION_ENGINE_OPCODES.md](EXECUTION_ENGINE_OPCODES.md).
- **Execution**: protopy
  1. creates a `PythonEnvironment` with the standard library path, the module search
     paths and `sys.argv`;
  2. takes a script path, a module name, a `-c` string or the REPL flag;
  3. compiles the source and executes the module body as `__main__`. As in CPython, a
     `main` function runs only if the script calls it.

## Command-line contract

`protopy` accepts `-c`, `-m`/`--module`, `--script`, `-p`/`--path`, `--stdlib`,
`-i`/`--repl`, `--dry-run`, `--bytecode-only`, `--trace` and `-h`/`--help`, and reports
distinct exit statuses so that tools can rely on them:

| Status | Meaning |
|--------|---------|
| 0 | Success. |
| 64 | Usage error, including running without a target. |
| 65 | The script or module could not be found. |
| 70 | Unhandled exception. |
| *n* | `SystemExit(n)` or `sys.exit(n)` with an integer *n*. |

The full description is in [USER_GUIDE.md](USER_GUIDE.md#exit-status).

## History

The step plan that led to this design, including the original choice of a minimal
executor, is kept in [archive/IMPLEMENTATION_PLAN.md](archive/IMPLEMENTATION_PLAN.md).
