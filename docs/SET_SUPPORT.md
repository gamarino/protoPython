# Set Support

Python `set` objects are backed by protoCore's `ProtoSet`: the `BUILD_SET` and
`SET_ADD` opcodes in `src/library/ExecutionEngine.cpp` build and extend a `ProtoSet`.

Supported operations include `set()`, `add`, `remove`, `len()`, `in`, truth testing,
iteration, and the `|`, `&` and `-` operators (union, intersection, difference).

```python
s = set()
s.add(1)
s.add(2)
s.remove(1)
print(len(s), 2 in s, bool(s), list(s))   # 1 True True [2]
print(s | {3}, s & {2}, s - {2})          # {2, 3} {2} set()
```

Known limitation: two distinct elements with equal hashes are treated as the same
element; see [CPYTHON_CONFORMANCE.md](CPYTHON_CONFORMANCE.md#known-divergences-pending).
