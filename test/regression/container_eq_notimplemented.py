"""list, tuple and dict __eq__ answer NotImplemented for other types."""
assert [1].__eq__((1,)) is NotImplemented and (1,).__eq__([1]) is NotImplemented
assert {}.__eq__([]) is NotImplemented and [].__eq__(None) is NotImplemented
assert ([1] == (1,)) is False and ([1] != (1,)) is True and ({} == []) is False
assert [1, 2] == [1, 2] and (1, 2) == (1, 2) and {'a': 1} == {'a': 1}


class L(list):
    pass


assert L([1]) == [1] and [1] == L([1]) and (1,) != [1]


class AnyEq:
    def __eq__(self, other):
        return True


# The other operand's __eq__ answers when the container declines.
assert [1] == AnyEq() and (1,) == AnyEq() and {} == AnyEq()
assert [1] in [[1]] and (1,) not in [[1]]
print("container eq notimplemented OK")
