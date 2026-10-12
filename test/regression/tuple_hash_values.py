# Tuple hashes keep their values: they are content hashes computed by
# protoPython (PythonEnvironment tupleKeyHash), not CPython's, and they did not
# change when tuples stopped being protoCore ProtoTuples.  The values below
# were printed by the ProtoTuple-based runtime (main d7291d10).
expected = {
    (): 3430009,
    (1,): 9627036882049,
    (1, 2): 6273171246137927309,
    (1, (2, 3)): 3139250177,
    (-1, 0): -6273171245873928563,
}
for t, h in expected.items():
    assert hash(t) == h, (t, hash(t), h)
    assert hash(tuple(list(t))) == h, t
print("tuple hash values OK")
