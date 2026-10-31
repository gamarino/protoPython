# Nested tuples that differ only by which element sits in which level hash
# apart. The tuple hash mixed elements as h = h * M ^ e, which cancels out for
# (a, (b, c)) against (b, (a, c)); a dict or set keys a tuple by its hash, so
# the two were one key. A search over (player, boxes) states lost every state
# whose player and first box traded places.

a = ((6, 6), ((5, 6), (7, 6)))
b = ((5, 6), ((6, 6), (7, 6)))
assert a != b
assert hash(a) != hash(b)
assert len({a, b}) == 2
assert b not in {a}
d = {a: "a", b: "b"}
assert len(d) == 2 and d[a] == "a" and d[b] == "b"

# Every (p, (q, r)) over a small grid is a distinct set element.
cells = [(x, y) for x in range(6) for y in range(4)]
states = [(p, (q, r)) for p in cells for q in cells for r in cells[:4]]
assert len(set(states)) == len(states), (len(set(states)), len(states))
assert len({hash(s) for s in states}) == len(states)

# Equal tuples still hash alike however they were built.
assert hash((1, (2, 3))) == hash(tuple([1, tuple([2, 3])]))
assert hash((1, 2)) == hash((1.0, 2)) == hash((True, 2.0))

print("tuple_hash_nested: ok")
