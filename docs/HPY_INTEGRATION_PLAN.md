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

## References

- [HPy Documentation](https://docs.hpyproject.org/)
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — Section 2 HPy Support
- [C_MODULES_TO_REPLACE.md](C_MODULES_TO_REPLACE.md)