# protoPython Documentation

This directory contains the technical documentation for protoPython, a high-performance Python 3.14-compatible runtime built on protoCore with GIL-free concurrency.

---

## For New Collaborators

**Start here.** If you are new to the project, read in this order:

1. **[PROTOPY_SCOPE.md](PROTOPY_SCOPE.md)** — What protoPython is and how it executes Python code.
2. **[DESIGN_DECISIONS.md](DESIGN_DECISIONS.md)** — Main architectural choices and rationale.
3. **[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)** — Roadmap, phases, and current status.

---

## Project Overview

- **Objective:** A GIL-less, high-performance Python 3.14 runtime leveraging protoCore's immutable object system and parallel architecture.
- **Status:** Phase 5 (HPy, REPL, error reporting) complete; Phase 6 (full stubs, threading, networking) in progress. Experimental; not production ready.
- **Key differentiator:** True parallel execution (no Global Interpreter Lock), zero-copy interop via HPy/UMD, and hardware-aligned memory layout.

---

## Document Index

### Getting Started

| Document | Description |
|----------|-------------|
| [INSTALLATION.md](INSTALLATION.md) | Build and install protoPython (Linux, macOS, Windows). |
| [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md) | Runtime scope: minimal bytecode executor, CLI, execution flow. |

### Architecture and Design

| Document | Description |
|----------|-------------|
| [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) | Consolidated summary of main architectural decisions. |
| [L_SHAPE_ARCHITECTURE.md](L_SHAPE_ARCHITECTURE.md) | Memory topology, M:N threads, context stack, lock-free hot path. |
| [POST_REFACTOR_AUDIT.md](POST_REFACTOR_AUDIT.md) | L-Shape refactor outcomes, mutex/atomic audit, slot fallback. |
| [TECHNICAL_AUDIT.md](TECHNICAL_AUDIT.md) | Deep technical audit: architecture, build, safety, concurrency, testing, packaging. |
| [REARCHITECTURE_PROTOCORE.md](REARCHITECTURE_PROTOCORE.md) | Target architecture for full protoCore integration. |

### Implementation

| Document | Description |
|----------|-------------|
| [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) | Phases, roadmap, completed and pending work. |
| [STUBS.md](STUBS.md) | Stub implementation status for stdlib and native modules. |
| [C_MODULES_TO_REPLACE.md](C_MODULES_TO_REPLACE.md) | C modules to replace with GIL-less C++ implementations. |
| [FULL_STUB_IMPLEMENTATION.md](FULL_STUB_IMPLEMENTATION.md) | Detailed stub implementation phases. |

### HPy (C Extensions)

| Document | Description |
|----------|-------------|
| [HPY_USER_GUIDE.md](HPY_USER_GUIDE.md) | How to load and use HPy extension modules. |
| [HPY_DEVELOPER_GUIDE.md](HPY_DEVELOPER_GUIDE.md) | How to write HPy modules for protoPython. |
| [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md) | HPy integration phases and design. |
| [HPY_REPL_INTEGRATION.md](HPY_REPL_INTEGRATION.md) | HPy integration with the REPL. |

### Reference

| Document | Description |
|----------|-------------|
| [EXECUTION_ENGINE_OPCODES.md](EXECUTION_ENGINE_OPCODES.md) | Supported bytecode opcodes. |
| [EXCEPTIONS.md](EXCEPTIONS.md) | Exception scaffolding and behavior. |
| [SET_SUPPORT.md](SET_SUPPORT.md) | Set prototype and operations. |

### Testing and Compatibility

| Document | Description |
|----------|-------------|
| [COMPATIBILITY_DASHBOARD.md](COMPATIBILITY_DASHBOARD.md) | Regrtest pass rate tracking and dashboard usage. |

### Roadmap

| Document | Description |
|----------|-------------|
| [PACKAGING_ROADMAP.md](PACKAGING_ROADMAP.md) | Packaging, distribution, and installation roadmap. |
