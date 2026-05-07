# Conceptual audit — protoCore + protoPython

**Started**: 2026-05-07.
**Scope**: protoCore (the core dynamic object system) and protoPython (the Python 3.14 runtime built on top of it).
**Owner**: gamarino.

## Why this audit exists

Recent debugging sessions surfaced four recurring patterns of bug:

1. **Fast paths that bypass dunder dispatch** — `binaryAdd`, `OP_CONTAINS`, `super().legit`, etc. each had a path optimised for the built-in type that silently ignored a Python-side override. Fixed one by one, but the pattern keeps appearing.
2. **Confusion between API tagged pointers and IMPL raw pointers in private fields** — `ProtoObjectCell::attributes` was declared `ProtoSparseList*` (API) but accessed as both API (via trampolines) and quasi-IMPL (via `sparseListGetRaw`). Defensive guards (`(uintptr_t)node & 0x3F != 0`) covered the inconsistency rather than fixing the root cause.
3. **GC root discipline gaps in C native functions** — `py_str_join` and similar held `ProtoObject*` iterators in C++ locals across deep callbacks; the operand-stack-roots-only model didn't see them; deep recursion freed them under the running mutator.
4. **Native module stubs whose semantics drift from CPython** — `_collections_abc.MutableMapping.update`, `Mapping.__contains__` were no-ops in CollectionsAbcModule, breaking UserDict, ChainMap, os._Environ silently for users.

These are not random bugs. They are symptoms of structural tensions that, individually, were patched but, collectively, indicate the system has reached the maturity inflection point where further bring-up without a conceptual cleanup keeps adding marginal cost.

The audit is **not** a rewrite. It produces:
- A documented map of the current architecture.
- A list of every site where a known anti-pattern recurs.
- Severity ratings.
- A prioritised remediation roadmap.

## Methodology

Each phase below produces a single Markdown document under `tasks/audit/` with a fixed structure:

1. **Inventory** — exhaustive list of the relevant sites (no sampling).
2. **Per-site analysis** — for each: current state, observed risk, severity, fix sketch.
3. **Aggregated findings** — patterns that emerged from the inventory.
4. **Action items** — concrete tasks, ranked.

Output is meant to survive this session. The next developer reading these documents should be able to pick up any individual finding without rereading everything else.

## Phases

### P0 — Plan (this document)

### P1 — ABI layer table (`01-layers.md`)
For each Cell-derived class:
- IMPL class name (e.g. `ProtoSparseListImplementation`).
- API class name (e.g. `ProtoSparseList`), if any.
- POINTER_TAG_* values that route to it.
- Conversion methods (`asObject`, `asXyz()`, `toImpl<>`).
- Where the field appears in private struct definitions.
- Whether the type used in those fields matches the abstraction layer (private fields → IMPL raw; public handles → API tagged).

The output is the canonical table that, going forward, every code reviewer can use to spot violations.

### P2 — Fast-path audit (`02-fast-paths.md`)
For each `if (a->isInteger())` / `if (a->isString())` / `if (a->isList())` / etc. in protoPython source:
- File:line.
- Operation name (e.g. `binaryAdd`, `OP_CONTAINS`).
- Does the path bypass dunder dispatch when type(a) is a Python subclass overriding the operation?
- Does the path bypass user `__contains__` / `__add__` / etc. when the LHS is built-in but the RHS is a subclass?
- Severity: HIGH (silent wrong result) / MEDIUM (raises in subclass case) / LOW (no observable difference).

### P3 — GC root discipline (`03-gc-roots.md`)
For each native C function in protoPython:
- Does it hold `ProtoObject*` values in C++ locals across an allocation boundary (any call that may trigger GC: bytecode execution, attribute lookup, method dispatch)?
- Are those locals reachable from a GC root (operand stack, ContextScope, ProtoRootSet)?
- If not, document the leak path concretely and propose the minimal pin.

### P4 — Native module stubs (`04-native-stubs.md`)
For each native module:
- Enumerate every method exposed.
- Compare against CPython's documented semantics.
- Flag no-ops, partial implementations, and silent-success stubs.
- For each gap: what real-world callers does it break?

### P5 — Synthesis (`00-summary.md`)
Cross-reference P1-P4. Group findings by root cause. Rank. Produce remediation roadmap. Update `tasks/lessons.md` with the architectural rules learned.

## Success criteria

- Every Cell-derived class is in the layer table (P1).
- Every fast-path is in the audit (P2).
- Every native function with C++-local cell pointers is documented (P3).
- Every native module method is mapped to CPython semantics (P4).
- The summary identifies the top 5 findings ranked by impact, each with a concrete fix path.

## What this audit explicitly is NOT

- It is not a security audit.
- It is not a performance audit.
- It is not a feature-completeness audit (CPython coverage of all stdlib).
- It is not a code-style review.
- It does not produce code fixes — only the map of where fixes are needed.

Code fixes are separate sessions, prioritised by the P5 roadmap.

## Time estimate

This is open-ended by user request. Initial guesses:

- P1: 2-3 hours (read every Cell definition; produce table).
- P2: 3-4 hours (find every fast-path site; analyse each).
- P3: 3-4 hours (read every native function; classify roots).
- P4: 2-3 hours (compare ~10 native modules with CPython spec).
- P5: 1-2 hours (synthesis).

Total: ~12-18 hours of focused work. Likely spread across multiple sessions. Each phase deliverable is independently useful.
