# C Modules to Replace for protoPython StdLib

This document lists Python standard library modules traditionally implemented in C (or with C accelerators) that protoPython aims to replace with GIL-less C++ implementations. See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) and [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md).

## Priority for early regrtest

- **High**: Required for basic imports and many regression tests.
- **Medium**: Needed for a substantial subset of tests or common stdlib.
- **Low**: Can be deferred; pure-Python fallbacks or stubs may suffice initially.

## Module list

| Module       | Priority | Status    | GIL-less note                          |
| ------------ | -------- | --------- | -------------------------------------- |
| `_collections` | High    | Replaced  | deque and helpers in CollectionsModule |
| `_functools`   | High    | Planned   | lru_cache, partial; use protoCore      |
| `_operator`    | High    | Planned   | Used by many stdlib modules            |
| `_io`          | High    | Replaced  | Basic open/file in IOModule            |
| `_socket`      | Medium  | Deferred  | Thread-safe APIs from the start        |
| `_ssl`         | Medium  | Deferred  | Build on _socket                       |
| `_json`        | Medium  | Planned   | Parser in C++; no GIL                  |
| `_pickle`      | Medium  | Deferred  | Accelerator; pure-Python fallback      |
| `_struct`      | Medium  | Planned   | Pack/unpack; small surface              |
| `_array`       | Medium  | Deferred  | Typed arrays                           |
| `_heapq`       | Medium  | Deferred  | Heap operations                        |
| `_random`      | Low     | Deferred  | Thread-local RNG                       |
| `_datetime`    | Low     | Deferred  | Date/time logic                        |
| `_hashlib`     | Low     | Deferred  | Hashing; use system/OpenSSL             |

## Status key

- **Replaced**: Implemented in protoPython C++ (e.g. in `src/library/`).
- **Planned**: Scheduled for replacement; scope and order may be refined.
- **Deferred**: Not required for early regrtest; will be GIL-less when implemented.

## References

- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — Section 2 Standard Library Integration
- [PROTOPY_SCOPE.md](PROTOPY_SCOPE.md) — Runtime and execution scope
