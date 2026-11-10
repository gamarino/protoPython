# Tuple hashes keep their values: they are content hashes computed by
# protoPython (PythonEnvironment tupleKeyHash), not CPython's. Since 1.2.2 they
# mix element hashes as protoCore 3.5.1's ProtoTuple::getHash does (CPython's
# xxHash round) over the elements' dict indices: an int n hashes as the
# SmallInteger tagged word of hash(n), (hash(n) << 10) | 1; -1, whose hash
# is -2, mixes as the word of -2. The values below were computed from that
# formula outside protoPython (CPython, arbitrary-precision arithmetic modulo 2**64).
expected = {
    (): 5740354900026072187,
    (1,): -2563183554774558118,
    (1, 2): 4617945697471146677,
    (1, (2, 3)): 2118435136792037492,
    (2, (1, 3)): -2707597811676806399,
    (-1, 0): -4328361412796650808,
}
for t, h in expected.items():
    assert hash(t) == h, (t, hash(t), h)
    assert hash(tuple(list(t))) == h, t
print("tuple hash values OK")
