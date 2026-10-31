# Tuple hashes keep their values: they are content hashes computed by
# protoPython (PythonEnvironment tupleKeyHash), not CPython's, and they did not
# change when tuples stopped being protoCore ProtoTuples.  The values below
# are those of CPython's xxHash-based mix over protoCore's element hashes,
# which replaced a multiply-then-XOR mix that gave (a, (b, c)) and (b, (a, c))
# one hash (see tuple_hash_nested.py).
expected = {
    (): 5740354900026072187,
    (1,): -2563183554774558118,
    (1, 2): 4617945697471146677,
    (1, (2, 3)): 2118435136792037492,
    (-1, 0): 8388427827613494690,
}
for t, h in expected.items():
    assert hash(t) == h, (t, hash(t), h)
    assert hash(tuple(list(t))) == h, t
print("tuple hash values OK")
