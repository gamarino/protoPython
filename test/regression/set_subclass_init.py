"""set.__init__ fills the set, so set subclasses can take their own arguments."""


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


class S(set):
    def __init__(self, it, tag=None):
        super().__init__(it)
        self.tag = tag


s = S([3, 1, 3], tag="t")
assert sorted(s) == [1, 3] and len(s) == 2 and s.tag == "t"


class S2(set):
    pass


assert sorted(S2((4, 5))) == [4, 5]


class S3(set):
    def __init__(self, *args):
        set.__init__(self, *args)


assert sorted(S3(range(3))) == [0, 1, 2]

x = {9}
x.__init__([7, 8])
assert x == {7, 8}
assert set(i for i in range(3)) == {0, 1, 2}
assert raises(TypeError, lambda: set([1], [2]))
assert raises(TypeError, lambda: set(it=[1]))


class F(frozenset):
    pass


assert F([1, 2]) == frozenset([1, 2])

print("set subclass init OK")
