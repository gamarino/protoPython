# String Support

This document describes string dunder methods and helpers implemented in protoPython.

## Dunder Methods

- **`__iter__`**: Returns an iterator over characters (single-character strings). Wrapped strings only; raw string iteration has known stability issues (segfault when returning raw ProtoStringIterator).
- **`__contains__`**: Substring membership. Works on wrapped strings (object with `strPrototype` and `__data__`).
- **`__bool__`**: Empty string is falsy; non-empty is truthy.

## Helpers

- **`upper()`**: ASCII uppercase. Non-ASCII bytes are left unchanged.
- **`lower()`**: ASCII lowercase. Non-ASCII bytes are left unchanged.
- **`strip()`**: Remove leading and trailing ASCII whitespace.
- **`lstrip()`**: Remove leading ASCII whitespace.
- **`rstrip()`**: Remove trailing ASCII whitespace.
- **`replace()`**: Replace occurrences of old with new; optional count.

## Dispatch

- Raw strings (from `context->fromUTF8String`) are primitives. `iter()` and `bool()` in builtins handle them directly.
- Wrapped strings (object with `strPrototype` as parent and `__data__` holding the ProtoString) use `strPrototype` dunders.
- Use `builtins.contains(item, container)` for substring checks on raw strings in tests.
