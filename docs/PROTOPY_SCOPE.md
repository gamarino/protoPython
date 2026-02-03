# protopy Scope and Architecture Decision

## Decision: Minimal bytecode executor inside protoPython (option B)

**protopy** will be implemented as a **minimal bytecode executor inside protoPython** that uses the existing `PythonEnvironment` and `ProtoContext`, rather than forking CPython 3.14 and swapping its eval loop.

## Rationale

- **Code location**: All runtime code stays in [src/runtime](../src/runtime/); no external CPython fork to maintain or sync.
- **Dependencies**: Build depends only on protoCore and the protoPython library; no CPython build tree required.
- **Incremental delivery**: We can ship a stub binary that creates `PythonEnvironment`, resolves modules, and runs a minimal execution path (e.g. load script path, then interpret bytecode) without implementing the full CPython bytecode set upfront.
- **GIL-less by design**: The executor is written for protoCore from the start; no need to remove GIL from CPython’s codebase.

## Implications

- **Frontend**: Parsing and compilation (source → bytecode) are out of scope for the first milestone. Options for a later phase: (a) embed or link a copy of CPython’s parser/compiler, or (b) implement a minimal parser/compiler in protoPython.
- **Bytecode format**: For minimal executor we can either adopt a subset of CPython 3.14 bytecode (and document which opcodes are supported) or define a minimal custom bytecode that we translate to from Python (e.g. via an external tool or a later-integrated frontend).
- **Execution**: The protopy binary will:
  1. Create `PythonEnvironment` with stdlib path and search paths.
  2. Accept a script path or module name.
  3. Resolve the module or load the file; if the module has a callable `main` attribute, invoke it (stub execution path). Once the full execution engine exists, bytecode will be run via `ProtoContext` and the protoPython object model.
- **CLI contract**: `protopy` now exposes `--module`, `--script`, `--path`, and `--stdlib` flags plus distinct exit codes (0 success, 65 resolve error, 70 runtime failure, 64 usage). This allows tooling to consume protopy deterministically.

## Revision

This document may be updated when we add the real bytecode format and execution loop (Phase 6). A future revision could record the choice of “CPython bytecode subset” vs “custom bytecode” and the chosen frontend (parser/compiler) source.
