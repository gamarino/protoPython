import functools
import json


def plain(a):
    """A docstring
    on two lines."""
    # a comment before the first statement

    b = a + 1
    return b


def deco(fn):
    @functools.wraps(fn)
    def wrapper(*args):
        return fn(*args)
    return wrapper


@deco
def decorated(a):
    """Decorated."""
    # comment
    return a * 2


class K:
    def method(self, a):
        """Method docstring."""
        # comment
        c = a - 1
        return c


def outer(a):
    def inner(x):
        """Inner docstring."""
        # comment
        return x + 100
    return inner(a)


plain(1)
decorated(2)
K().method(3)
outer(4)
s = json.dumps([1])
print("done", s)
