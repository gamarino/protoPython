# Contributing to protoPython

protoPython is a GIL-free Python 3.14 runtime built on
[protoCore](https://github.com/numaes/protoCore). Bug reports and pull requests
are welcome at <https://github.com/gamarino/protoPython>.

## Prerequisites

- **Linux.** The runtime uses POSIX system calls and Linux-specific interfaces
  such as `/proc/self/exe`; other platforms are not currently tested.
- **A C++20 compiler:** GCC or Clang (the build passes GCC/Clang-specific flags
  such as `-fno-delete-null-pointer-checks`).
- **CMake 3.20 or newer.**
- **protoCore**, in one of two ways:
  - *Development (default):* check protoCore out next to this repository as
    `../protoCore`. CMake builds it as part of the protoPython build.
  - *Installed:* pass `-DPROTO_CORE_PREFIX=<prefix>`, where `<prefix>` contains
    `lib/libprotoCore` (or `lib64/`) and `include/protoCore.h`.
- **GoogleTest** for the C++ unit tests. In a development build it comes from
  protoCore's test build, which downloads GoogleTest 1.14.0 with CMake
  `FetchContent` when you configure (network access is needed the first time).
  With `PROTO_CORE_PREFIX`, the `gtest_main` library the tests link against
  must be installed on the system.
- **Python 3** (optional): used by the `regrtest_persistence` test and by the
  benchmark and conformance scripts.

## Getting the sources

```bash
git clone https://github.com/numaes/protoCore.git
git clone https://github.com/gamarino/protoPython.git
cd protoPython
```

## Building

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
```

This produces the interpreter `build_release/src/runtime/protopy` and the
ahead-of-time compiler `build_release/src/compiler/protopyc`. To build against
an installed protoCore instead of `../protoCore`:

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DPROTO_CORE_PREFIX=/usr/local
```

Build directories (`build*/`, `cmake-build-*/`) are ignored by `.gitignore`.
The conformance and debug scripts in `tests/` expect the interpreter under
`build_release/`.

## Running the tests

```bash
ctest --test-dir build_release --output-on-failure
```

Useful subsets:

```bash
ctest --test-dir build_release -L regression_gate      # tests labelled regression_gate
ctest --test-dir build_release -R protopy_system_exit  # tests whose name matches a pattern
```

Where the tests live:

| Location | Contents | Registered in |
|---|---|---|
| `test/library/` | GoogleTest unit tests (`test_foundation`, `test_execution_engine`, `test_compiler`, ...) | `test/library/CMakeLists.txt` |
| `test/regression/` | `test_regr` (GoogleTest) and the Python regression scripts run by `protopy` | `test/regression/CMakeLists.txt` and the top-level `CMakeLists.txt` |
| `test/compiler/` | `run_module`, a helper the benchmark suite uses to load protopyc-generated modules (not a test) | `test/compiler/CMakeLists.txt` |

Configuring with `-DPROTOPY_BENCHMARKS=ON` also registers the benchmark suite
as the `protopy_benchmarks` test (off by default).

### Adding a regression test

A bug fix should come with a regression test that fails without the fix:

1. Add `test/regression/<name>.py`. It checks the expected behaviour with
   `assert` or explicit comparisons; an uncaught exception makes `protopy`
   exit with a non-zero status, which fails the test.
2. Register it in the top-level `CMakeLists.txt`, next to the existing
   entries:

   ```cmake
   # One-line description of the behaviour under test.
   add_test(NAME protopy_<name>
       COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/<name>.py)
   set_tests_properties(protopy_<name> PROPERTIES
       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
       TIMEOUT 120
       LABELS "regression_gate")
   ```

3. To check a process exit status instead, wrap the command with
   `test/regression/assert_exit.sh <expected-status> <command...>`, as the
   `protopy_system_exit_*` tests do.

## CPython conformance

`test/cpython/` holds test files from the CPython test suite and small probe
scripts. `tests/run_conformance.sh` (or `tests/run_conformance.py`) runs a
fixed list of those files and custom tests with
`build_release/src/runtime/protopy`, a 120-second timeout per file, and writes
PASS / FAIL / TIMEOUT lines to `tests/conformance_status.log`.

The current conformance status and the known divergences from CPython are
documented in [docs/CPYTHON_CONFORMANCE.md](docs/CPYTHON_CONFORMANCE.md).

## File placement

- Registered regression tests go in `test/regression/`.
- Files from the CPython test suite and probes against it go in `test/cpython/`.
- Ad-hoc reproductions and diagnostic scripts go in `tests/`, never in the
  repository root. Do not commit their output: `.txt` and `.log` files at the
  top level of `tests/`, and `*.log` files anywhere, are ignored by
  `.gitignore`.

## Native code and the garbage collector

protoCore's collector does not see `ProtoObject*` values held only in C++
locals, lambdas, threads or containers outside protoCore. Before writing
native code that keeps such a pointer across a call that may run Python code
or allocate, read [docs/GC_BRIDGING.md](docs/GC_BRIDGING.md) and apply its
code-review checklist.

## Documentation

- Write documentation, code comments and messages in professional English.
- Do not put absolute local paths in documentation, scripts or comments; use
  paths relative to the repository (scripts can locate the repository from
  their own location, as `tests/run_conformance.sh` does).
- Link to other proto repositories with absolute GitHub URLs; relative links
  such as `../protoCore/...` leave the repository and do not work on GitHub.
- Benchmark reports go in `benchmarks/reports/` with the measurement date in
  the file name (for example `2026-06-16-full-stack-cpp-proto-python.md`). A
  report records one measurement and is not rewritten later; publish a new
  report instead. Performance figures quoted in `README.md` or `docs/` should
  cite the report they come from.

## Commits and pull requests

- Keep each commit to one logical change. Subjects in the history follow a
  `type(area): summary` form, for example `fix(dict): ...`,
  `perf(interpreter): ...` or `docs: ...`. The body explains the root cause
  and names the regression test (`Test: test/regression/<name>.py`).
- Build and run the full `ctest` suite before opening a pull request. The
  repository has no continuous integration, so state in the pull request
  which tests you ran and on which platform.
- For performance changes, state how you measured: build type, benchmark
  script (for example `benchmarks/run_full_stack.py` or
  `benchmarks/run_4way_interleaved.py`), number of runs, and the before and
  after figures.
- Changes to protoCore itself belong in the
  [protoCore repository](https://github.com/numaes/protoCore).

## License

protoPython is released under the MIT License (see [LICENSE](LICENSE)). By
contributing, you agree that your contributions are licensed under the same
license.
