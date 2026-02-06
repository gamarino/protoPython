# Packaging and Distribution Roadmap

This document outlines the path to distributing protoPython via standard Python packaging and installation channels.

## Goals

- **pip installable**: Users can install protoPython (or a compatible package) via `pip`.
- **Wheels**: Provide platform wheels (e.g. manylinux, macOS, Windows) for the native runtime.
- **Drop-in replacement**: Support aliasing `protopy` to `python` in scripts and tooling where feasible.

## Current State

- protoPython builds as a C++ project (CMake, protoCore dependency).
- The `protopy` binary and `libprotoPython.so` are produced in the build tree.
- Standard library lives under `lib/python3.14/` and is located via `--path` or build-time path.

## Roadmap

1. **Build artifacts**: Define a minimal install layout (binary, shared library, stdlib path) and document `CMAKE_INSTALL_PREFIX` usage.
2. **Wheel layout**: Design a wheel that contains:
   - `protopy` (or platform-specific executable)
   - Shared library for the runtime
   - Bundled stdlib (or reference to sys.path)
3. **pip integration**: Provide a `setup.py` or `pyproject.toml` that builds the C++ project and packages the wheel. May use CMake as a build backend or a wrapper that invokes CMake.
4. **Virtual environments**: Document how to use protoPython inside a `venv` (e.g. copy binary and lib into venv bin/ and lib/, or use `--path` to point at venv).
5. **Drop-in replacement**: Document limitations (e.g. no full CPython C-API); recommend `PROTOPY_BIN` for scripts that need to invoke protoPython explicitly.

## References

- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — Section 7 Installation & Distribution
- [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md) — Runtime and execution scope
