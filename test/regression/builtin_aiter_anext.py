# The builtins aiter() and anext() (Python 3.10).  They did not exist
# (NameError); CPython's test_asyncgen uses both.
import asyncio


async def gen():
    yield 1
    yield 2


class Counter:
    def __init__(self):
        self.n = 0

    def __aiter__(self):
        return self

    async def __anext__(self):
        self.n += 1
        if self.n > 2:
            raise StopAsyncIteration
        await asyncio.sleep(0)
        return self.n


async def main():
    g = gen()
    assert aiter(g) is g
    assert await anext(g) == 1 and await anext(g) == 2
    try:
        await anext(g)
    except StopAsyncIteration:
        pass
    else:
        raise AssertionError("anext past the end")
    assert await anext(g, "end") == "end"
    c = Counter()
    assert [await anext(c, None) for _ in range(3)] == [1, 2, None]
    for bad in (1, [1]):
        try:
            anext(bad)
        except TypeError:
            pass
        else:
            raise AssertionError("anext(%r)" % (bad,))
    try:
        aiter([])
    except TypeError:
        pass
    else:
        raise AssertionError("aiter([])")
    return "done"


assert asyncio.run(main()) == "done"
print("builtin_aiter_anext: ok")
