# Next 100 Steps: HPy Implementation

Master plan for implementing [HPy](https://hpyproject.org/) support in protoPython (steps 1185–1284). Aligned with [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md) and [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) §2.

**Rule:** Every 20 steps (one block), document and commit all new files and changes.

---

## Block Overview

| Block | Steps   | Theme | Doc | Commit focus |
|-------|---------|--------|-----|--------------|
| **v63** | 1185–1204 | Phase 1 foundation: handle table, HPyContext, core ABI (handles, attr, call) | [NEXT_20_STEPS_V63.md](NEXT_20_STEPS_V63.md) | HPy context + handle table + minimal ABI |
| **v64** | 1205–1224 | Phase 1 completion: module load, HPyModuleDef, init; Phase 2 start (universal ABI) | NEXT_20_STEPS_V64.md | Load .so/.hpy.so, init, universal ABI resolution |
| **v65** | 1225–1244 | Phase 2: universal ABI loading; Phase 3 start (API coverage) | NEXT_20_STEPS_V65.md | Entry points, import wiring, API subset |
| **v66** | 1245–1264 | Phase 3: number/sequence protocols, exceptions, object creation | NEXT_20_STEPS_V66.md | HPy API coverage for common extensions |
| **v67** | 1265–1284 | Phase 4: documentation, tooling, build/distribute | NEXT_20_STEPS_V67.md | Docs, tooling, final tests, commit |

---

## Phase Mapping

- **Phase 1 (HPy runtime shim):** v63–v64 — context, handle table, core ABI, module load and init.
- **Phase 2 (Universal ABI):** v64–v65 — single binary, no Python-version coupling; resolve HPyModuleDef and init.
- **Phase 3 (API coverage):** v65–v66 — object creation, attr, call, number/sequence, exceptions.
- **Phase 4 (Ecosystem):** v67 — build/distribute docs, compatibility with HPy tooling.

---

## Per-Block Workflow

1. **Implement** the 20 steps (code, tests, minimal docs).
2. **Document** in `NEXT_20_STEPS_Vxx.md` (step table, status, summary).
3. **Update** `tasks/todo.md`, `IMPLEMENTATION_PLAN.md`, `STUBS.md`, `TESTING.md` as needed.
4. **Commit** with message: `feat(hpy): Next 20 Steps vxx (steps xxxx–xxxx); ...`.

---

## References

- [HPY_INTEGRATION_PLAN.md](HPY_INTEGRATION_PLAN.md)
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — Section 2 HPy Support
- [HPy Documentation](https://docs.hpyproject.org/)
- [HPy API](https://docs.hpyproject.org/en/latest/api.html)
