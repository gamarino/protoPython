# protoPython Documentation

protoPython is a GIL-free Python 3.14 runtime built on
[protoCore](https://github.com/numaes/protoCore). This page indexes the documentation in
the repository.

**Status:** version 1.0.0; not production ready.
[CPYTHON_CONFORMANCE.md](CPYTHON_CONFORMANCE.md) records the current conformance status,
and its [known divergences](CPYTHON_CONFORMANCE.md#known-divergences-pending) list what
is still pending.

---

## Using protoPython

| Document | Description |
|----------|-------------|
| [README.md](../README.md) | Project overview, status, benchmarks, quick start and architecture. |
| [INSTALLATION.md](INSTALLATION.md) | Building, testing and installing protoPython (Linux), with protoCore built alongside or installed. |
| [USER_GUIDE.md](USER_GUIDE.md) | `protopy` command line, exit statuses, module search path, REPL, threads, troubleshooting. |
| [PYTHON_COMPATIBILITY.md](PYTHON_COMPATIBILITY.md) | Supported syntax, built-in types and modules, and notable differences from CPython. |
| [CPYTHON_CONFORMANCE.md](CPYTHON_CONFORMANCE.md) | Current conformance status and known divergences from CPython. |
| [COMPATIBILITY_DASHBOARD.md](COMPATIBILITY_DASHBOARD.md) | Running CPython regression tests with `test/regression/run_and_report.py` and tracking the pass rate. |

## Architecture and design

| Document | Description |
|----------|-------------|
| [DESIGN.md](../DESIGN.md) | Technical design: components, type representation, bytecode, concurrency. |
| [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) | Summary of the main architectural decisions, with links to the detailed documents. |
| [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md) | Why `protopy` is a bytecode executor inside protoPython rather than a CPython fork; its command-line contract. |
| [INTERNALS_DEEP_DIVE.md](INTERNALS_DEEP_DIVE.md) | Memory model, execution engine, structural sharing and the zero-copy principle. |
| [L_SHAPE_ARCHITECTURE.md](L_SHAPE_ARCHITECTURE.md) | Single `ProtoSpace`, M:N threads, explicit context stack and lock-free hot paths. |
| [REARCHITECTURE_PROTOCORE.md](REARCHITECTURE_PROTOCORE.md) | Target architecture and gap analysis for running natively on protoCore. |
| [DESIGN_PROTOCORE_DELEGATION.md](DESIGN_PROTOCORE_DELEGATION.md) | Design (2026-04-28) for delegating Python attribute access to protoCore. |
| [GC_BRIDGING.md](GC_BRIDGING.md) | Keeping `ProtoObject` references alive across boundaries the protoCore garbage collector cannot see, and GC root discipline in native functions. |

## Developer reference

| Document | Description |
|----------|-------------|
| [CPP_API_REFERENCE.md](CPP_API_REFERENCE.md) | Embedding API: `PythonEnvironment` construction, execution, errors, object access, and protoCore value conversions. |
| [PROTOPYC_SPECIFICATION.md](PROTOPYC_SPECIFICATION.md) | The `protopyc` compiler: command line, loading generated modules, generated code, unimplemented features. |
| [EXECUTION_ENGINE_OPCODES.md](EXECUTION_ENGINE_OPCODES.md) | Reference of all opcodes defined in `ExecutionEngine.h`: instruction format, fused opcodes, meaning of each opcode. |
| [HPY_DEVELOPER_GUIDE.md](HPY_DEVELOPER_GUIDE.md) | The HPy-style C++ extension API and how to write a module against it. |
| [HPY_USER_GUIDE.md](HPY_USER_GUIDE.md) | How the HPy extension loader works and its current status. |
| [HPY_REPL_INTEGRATION.md](HPY_REPL_INTEGRATION.md) | Status of HPy support in the `protopy` REPL. |
| [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md) | Original scope and phases of the HPy integration, with a status note. |
| [C_MODULES_TO_REPLACE.md](C_MODULES_TO_REPLACE.md) | Standard library modules CPython implements in C, and how protoPython provides each one. |
| [STUBS.md](STUBS.md) | Catalogue of native and standard library stub implementations kept during early development. |
| [EXCEPTIONS.md](EXCEPTIONS.md) | The native `exceptions` module and how native code reports exceptions. |
| [SET_SUPPORT.md](SET_SUPPORT.md) | `set` objects backed by `ProtoSet`. |
| [tests/conformity/README.md](../tests/conformity/README.md) | Layout and usage of the conformity test suite. |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | Prerequisites, building, running and adding tests, file placement, documentation, commits and pull requests. |
| [CHANGELOG.md](../CHANGELOG.md) | Notable changes by release. |

## Dated analyses

Written at a point in time; their numbers and conclusions reflect that date.

| Document | Description |
|----------|-------------|
| [EXECUTION_ENGINE_REGRESSIONS.md](EXECUTION_ENGINE_REGRESSIONS.md) | Execution engine regressions and their fixes (February 2026). |
| [POST_REFACTOR_AUDIT.md](POST_REFACTOR_AUDIT.md) | Technical audit with a revision history from the L-Shape refactor to v1.0.0 (2026-04-20). |
| [PERFORMANCE_ANALYSIS_2026-04-28.md](PERFORMANCE_ANALYSIS_2026-04-28.md) | Profile-based analysis of the interpreter overhead against CPython (2026-04-28). |
| [2026-06-15-overhead-diagnosis.md](2026-06-15-overhead-diagnosis.md) | Overhead diagnosis and optimisation plan (2026-06-15). |
| [2026-06-15-step-2-dispatch-investigation.md](2026-06-15-step-2-dispatch-investigation.md) | Dispatch-loop investigation from that plan; null result (2026-06-15). |
| [2026-06-15-step-6-list-mutable-deferred.md](2026-06-15-step-6-list-mutable-deferred.md) | "List mutable when owned" optimisation, deferred pending a protoCore design decision (2026-06-15). |
| [2026-06-15-final-comparison.md](2026-06-15-final-comparison.md) | Comparative results of the 2026-06-15 optimisation pass. |

## Audits

Dated audit reports in [audits/](audits/).

| Document | Description |
|----------|-------------|
| [audits/00-plan.md](audits/00-plan.md) | Conceptual audit of protoCore and protoPython (started 2026-05-07): scope and methodology. |
| [audits/00-summary.md](audits/00-summary.md) | Findings grouped by root cause and remediation roadmap. |
| [audits/01-layers.md](audits/01-layers.md) | ABI layer table: public API classes versus implementation classes. |
| [audits/02-fast-paths.md](audits/02-fast-paths.md) | Fast paths that may bypass dunder dispatch. |
| [audits/03-gc-roots.md](audits/03-gc-roots.md) | GC root discipline in native C++ functions. |
| [audits/04-native-stubs.md](audits/04-native-stubs.md) | Native module stubs compared with CPython semantics. |
| [audits/05-os-thread-math-functions.md](audits/05-os-thread-math-functions.md) | Per-function severity inventory for the os, thread and math native modules. |
| [audits/audit_2026-05-05.md](audits/audit_2026-05-05.md) | `protopy` ground-truth audit of the CPython test categories (2026-05-05). |
| [audits/audit_2026-05-07.md](audits/audit_2026-05-07.md) | `protopy` ground-truth audit of the CPython test categories (2026-05-07). |

## Benchmarks

- [benchmarks/reports/](../benchmarks/reports/) — dated benchmark reports (2026-04-28 to 2026-06-16); each is a snapshot of its date.
- [benchmarks/](../benchmarks/) — benchmark workloads and runners (`run_benchmarks.py`, `run_4way_interleaved.py`, `run_full_stack.py`, `pyperf/`).

## Archive

- [archive/](archive/README.md) — superseded plans, roadmaps and the conformance history log.
- [archive/design-specs/](archive/design-specs/README.md) — historical design specifications.
