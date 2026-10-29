# bool subclasses int, not the reverse: an int is never a bool instance.
# isinstance(1, bool) used to be True, so `isinstance(x, bool)` checks
# (abc's __subclasshook__ result check, argument validation in the stdlib)
# accepted any int.
assert not isinstance(1, bool)
assert not isinstance(0, bool)
assert not isinstance(-5, bool)
assert not isinstance(2 ** 70, bool)
assert isinstance(True, bool) and isinstance(False, bool)
assert isinstance(True, int) and isinstance(1, int)
assert not issubclass(int, bool) and issubclass(bool, int)
assert isinstance(1, (str, int)) and not isinstance(1, (str, bool))
assert [type(x).__name__ for x in (1, True)] == ["int", "bool"]


class MyInt(int):
    pass


assert not isinstance(MyInt(3), bool) and isinstance(MyInt(3), int)
print("isinstance_int_bool: ok")
