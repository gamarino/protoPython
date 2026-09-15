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
- `run_conformity.py`: the runner.
- `scripts/check_const_cast.sh`: lists `const_cast` uses in module and execution code
  for review.

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
script passes, 1 when any fails and 2 when it finds no binary. Set `PROTO_PYTHON`:
without it the runner looks for an executable named `protoPython` (in the repository
root, `build/` or the current directory), which the build does not produce. If the
manifest is missing or empty, the runner runs every `.py` file in `builtins/` and
`import/` except `conformity_dummy.py`. On 2026-09-15 all 10 listed scripts pass.

## `const_cast` check

```bash
./tests/conformity/scripts/check_const_cast.sh
```

The script searches `src/library/PythonEnvironment.cpp`,
`src/library/ExecutionEngine.cpp` and `src/library/SysModule.cpp` for
`const_cast<...ProtoObject...>`, prints every match and exits with status 1 if there is
any. The current sources contain such casts, so the script reports them and exits with
status 1; treat its output as a list of sites to review, not as a pass/fail gate.
