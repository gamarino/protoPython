# Phase 1.1.1: int isolation — arithmetic, identity, no in-place mutation.
# Immutability: operations return new roots; no mutation of numeric objects.

assert isinstance(42, int)
assert 1 + 2 == 3
assert 10 - 3 == 7
assert 2 * 3 == 6
assert 7 // 2 == 3
assert 7 % 2 == 1

# Identity: same value, small int cache behaviour (implementation-defined)
a = 1
b = 1
assert a == b

# Coercion / type consistency
assert int(3.14) == 3
assert isinstance(int("42"), int)

print("OK int conformity")
