# String Support

This document describes string dunder methods and helpers implemented in protoPython.

## protoCore representation (v58–v59)

- **Ropes as ProtoTuple**: Long strings are represented exclusively as ProtoTuple. No dedicated rope node type. Concat = one ProtoTuple with 2 slots (left string, right string) and `actual_size` = total length; leaf = tuple of 1..TUPLE_SIZE character objects. See [protoCore/docs/ROPES_AS_PROTOTUPLE.md](../../protoCore/docs/ROPES_AS_PROTOTUPLE.md).
- **Inline strings**: Up to 7 UTF-32 code units (ASCII 0–127) are stored in the tagged pointer (EMBEDDED_TYPE_INLINE_STRING); zero cell allocation.
- **Indexing**: getAt(index) traverses the tuple tree by length (concat: if index < leftSize then recurse left else recurse right; leaf: slot[index]). Cost is O(depth) then O(1) at leaf.
- **Iterator**: O(1) amortized per character; iterator holds string (inline or cell) and index, uses getProtoStringSize and getProtoStringGetAt.

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
- **`startswith()`**: Check if string starts with prefix.
- **`endswith()`**: Check if string ends with suffix.

## Dispatch

- Raw strings (from `context->fromUTF8String`) are primitives. `iter()` and `bool()` in builtins handle them directly.
- Wrapped strings (object with `strPrototype` as parent and `__data__` holding the ProtoString) use `strPrototype` dunders.
- Use `builtins.contains(item, container)` for substring checks on raw strings in tests.

## Block 1100-1200 V2 (v58–v62)

Ropes (ProtoTuple), inline strings, ProtoExternalBuffer, GetRawPointer API, and Swarm tests completed in v58–v61. v62: Swarm hardening (disabled 1M concat / large rope tests documented), lessons consolidated. See [NEXT_20_STEPS_V62.md](NEXT_20_STEPS_V62.md).
