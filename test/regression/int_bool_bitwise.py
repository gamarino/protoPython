# int's bitwise methods return an int even for two bools; bool's own return a
# bool for two bools and have the reflected forms (CPython).
assert int.__and__(True, True) == 1 and type(int.__and__(True, True)) is int
assert int.__or__(False, True) == 1 and type(int.__or__(False, True)) is int
assert int.__xor__(True, False) == 1 and type(int.__xor__(True, False)) is int
assert bool.__and__(True, True) is True
assert bool.__or__(False, False) is False
assert bool.__xor__(True, True) is False
assert True.__rand__(True) is True
assert False.__ror__(True) is True
assert True.__rxor__(False) is True
assert True.__rand__(3) == 1 and type(True.__rand__(3)) is int
assert (True & True) is True and (True | 2) == 3 and (2 ^ True) == 3
print("int_bool_bitwise: ok")
