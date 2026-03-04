# Phase 1.1.3: list isolation — append, extend, index assign, slice.
# Immutability: ProtoList updates use structural sharing; result is new root stored in scope.

L = []
L.append(1)
L.append(2)
assert L == [1, 2]

L.extend([3, 4])
assert L == [1, 2, 3, 4]

# Index assign (if supported)
L[0] = 10
assert L[0] == 10
assert L == [10, 2, 3, 4]

# Slice
sub = L[1:3]
assert sub == [2, 3]

print("OK list conformity")
