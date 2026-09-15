# Conformity Tests

Python scripts that check properties protoPython must keep on protoCore's immutable
object model: operations on built-in values produce new values instead of mutating
shared ones, and modules keep a consistent identity and content across mutation and
re-import. The suite was defined together with protoJS; the combined progress log of
that effort is archived in the protoJS repository
([docs/archive/CONFORMITY_PROGRESS.md](https://github.com/gamarino/protoJS/blob/master/docs/archive/CONFORMITY_PROGRESS.md)).

## Layout

- `builtins/`: `int` arithmetic and identity, `str` operations returning new values,
  `list` structural sharing, and `dict` get, set, delete and iteration.
- `import/`: module identity after `M.x = v`, the module wrapper pointing at the current
  module content, re-import without dangling references, and a cross-module
  attribute-cache regression. `conformity_dummy.py` is a helper module imported by these
  tests.
- `bootstrap/cpython_bootstrap.txt`: the scripts the runner executes, one path per line
  relative to the repository root (`#` starts a comment). It lists the `builtins/` and
  `import/` tests plus `tests/test_str.py` and `tests/test_list_iter.py`.
- `run_conformity.py`: the runner; `test_run_conformity.py` is its self-test, run
  with the host Python interpreter.
- `scripts/check_const_cast.sh`: reports `const_cast` uses in module and execution
  code for review.

## Running

Run one test from the repository root with the protopy binary:

```bash
build_release/src/runtime/protopy tests/conformity/builtins/test_int_conformity.py
```

Run the whole list with the runner:

```bash
PROTO_PYTHON=build_release/src/runtime/protopy python3 tests/conformity/run_conformity.py
```

The runner executes each listed script from the repository root with a 30-second
timeout and prints `[PASS]` or `[FAIL]` for each. It exits with status 0 when every
script passes, 1 when any fails and 2 when it finds no binary. The binary is the first
of: the `PROTO_PYTHON` environment variable, `build_release/src/runtime/protopy`,
`build/src/runtime/protopy`, and `protopy` on `PATH`. Each script runs with
`tests/conformity/import` prepended to `PROTO_PYTHONPATH`, the module search path
protopy reads. If the manifest is missing or empty, the runner runs every `.py` file in
`builtins/` and `import/` except `conformity_dummy.py`.

CTest runs the suite against the protopy it builds (`conformity_suite`), together with
the runner's self-test (`conformity_runner_selftest`) and the `const_cast` report
(`conformity_const_cast_report`); all three carry the `regression_gate` label.

## `const_cast` check

```bash
./tests/conformity/scripts/check_const_cast.sh
```

The script counts `const_cast<...ProtoObject...>` matches in
`src/library/PythonEnvironment.cpp`, `src/library/ExecutionEngine.cpp` and
`src/library/SysModule.cpp`, prints the count per file and the total, and exits with
status 0. `--list` also prints every matching line, and `--strict` exits with status 1
when there is any match.

A match is not a defect by itself: casting away `const` is the usual idiom for
protoCore's const-returning API, for example on the result of `ctx->newObject(true)` or
on a mutable frame passed by reference. Treat the output as a list of sites to review
for mutation of shared, immutable state, not as a pass/fail gate.
