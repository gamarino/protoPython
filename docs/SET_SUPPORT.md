# Set Support

Python `set` and `frozenset` objects keep their elements in `__data__`, a *set
table*: a protoCore `ProtoSparseList` indexed by the element's key hash, the
hash dict keys are filed under (so `1`, `1.0` and `True` are one element, and a
class's own `__hash__` is honoured). A slot holds

- the element itself, when it is the only element with that hash and the hash
  identifies it (built-in primitives and identity-hashed objects); or
- a bucket, a `ProtoList` of the distinct elements sharing that hash, for any
  element with a user-defined `__hash__` and for colliding elements.

Membership compares a candidate with the elements of a bucket by identity, then
with `__eq__`, as CPython does; an exception raised by `__eq__` propagates.
Distinct elements that share a hash are therefore kept apart, which protoCore's
`ProtoSet` (one element per hash) could not do.

The layout is implemented and documented under "Set storage" in
`src/library/PythonEnvironment.cpp`. Code elsewhere (the `BUILD_SET`, `SET_ADD`
and `SET_UPDATE` opcodes, protopyc's generated code, the `select` and `signal`
modules) goes through `PythonEnvironment::newSetStorage`, `setAdd`,
`setStorage`, `setSize`, `setElements` and `newSetObject` and never reads a
table directly.

```python
s = set()
s.add(1)
s.add(2)
s.remove(1)
print(len(s), 2 in s, bool(s), list(s))   # 1 True True [2]
print(s | {3}, s & {2}, s - {2})          # {2, 3} {2} set()
```

Iteration order follows the hash order of the elements, and the order of
insertion within a bucket; as in CPython, it is not part of the language.
