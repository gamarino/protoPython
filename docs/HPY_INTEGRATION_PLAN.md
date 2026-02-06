# HPy Integration Plan for protoPython

This document outlines the scope and approach for integrating [HPy](https://hpyproject.org/) (a portable API for Python C extensions) with protoPython.

## Goal

Allow HPy-compiled extension modules (`.so` / `.hpy.so`) to load and run under protoPython, so that the ecosystem of HPy extensions can be used without a GIL.

## Runtime Shim Scope

1. **HPy context**: Implement a protoPython-specific HPy context that maps HPy handles to `ProtoObject` instances. Each thread or context that uses HPy gets its own handle table.
2. **Core ABI**: Provide the minimal HPy ABI so that modules can be loaded:
   - Handles: `HPy_FromPyObject`, `HPy_AsPyObject`, `HPy_Dup`, `HPy_Close`
   - Type/attr: `HPy_Type`, `HPy_GetAttr`, `HPy_SetAttr`, `HPy_HasAttr`
   - Call: `HPy_Call`
   - Number/sequence protocols as needed by target extensions

## ABI Mapping

| HPy operation | protoPython equivalent |
|--------------|-------------------------|
| Handle ↔ PyObject* | Handle table: handle id → ProtoObject* |
| HPy_GetAttr | ProtoObject::getAttribute |
| HPy_SetAttr | ProtoObject::setAttribute |
| HPy_Call | Invoke callable via ProtoContext |
| HPy_Type | getPrototype / type object |

## Phases (from IMPLEMENTATION_PLAN)

- **Phase 1**: HPy runtime shim (context, handle table, core ABI).
- **Phase 2**: Universal ABI support (single binary, no Python-version coupling).
- **Phase 3**: API coverage for common extensions.
- **Phase 4**: Documentation and tooling (build, distribute HPy extensions for protoPython).

## Phase 1 Design (v55)

### Handle table

- **Per-context handle table**: Map `HPy` handle IDs to `ProtoObject*`. Handles are opaque; internally an index into a growable table.
- **Allocation**: `HPy_FromPyObject` inserts `ProtoObject*` and returns new handle. `HPy_Close` releases the handle slot.
- **Lookup**: `HPy_AsPyObject` resolves handle to `ProtoObject*` for native calls.

### HPyContext skeleton

- **ProtoPython HPy context**: Struct holding handle table and `ProtoContext*`. One per thread or per import session.
- **Entry points**: HPy modules expect `HPyModuleDef` with init function; protoPython resolves and invokes, passing HPy context.

### Core ABI (Phase 1 minimal)

| HPy API | protoPython implementation |
|---------|----------------------------|
| HPy_FromPyObject | Insert ProtoObject* into handle table; return handle |
| HPy_AsPyObject | Lookup handle; return ProtoObject* (or cast) |
| HPy_Dup | Increment ref / duplicate handle |
| HPy_Close | Release handle slot |
| HPy_GetAttr | ProtoObject::getAttribute |
| HPy_SetAttr | ProtoObject::setAttribute |
| HPy_Call | Invoke callable via ProtoContext |

## Implementation: Next 100 Steps

HPy is implemented in 5 blocks of 20 steps (1185–1284). Each block is documented and committed separately. See [NEXT_100_STEPS_HPY.md](NEXT_100_STEPS_HPY.md). v63 (1185–1204): Phase 1 foundation (context, handle table, core ABI). v64–v67: Phase 1 completion, universal ABI, API coverage, ecosystem.

## References

- [HPy Documentation](https://docs.hpyproject.org/)
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — Section 2 HPy Support
- [NEXT_100_STEPS_HPY.md](NEXT_100_STEPS_HPY.md) — 100-step implementation plan
- [C_MODULES_TO_REPLACE.md](C_MODULES_TO_REPLACE.md)