# Regression test: asynchronous comprehensions finish and give their values.
#
# Compiler::compileComprehension's async branch patched the argument of the
# instruction after the intended one (three addPatch(slot + 1, ...) calls),
# gave JUMP_FORWARD (relative) an absolute target, and loaded
# StopAsyncIteration with a NULL marker: `[x async for x in agen()]` yielded
# None forever.
import asyncio


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


async def agen(n):
    for i in range(n):
        yield i


class ARange:
    def __init__(self, n):
        self.i = 0
        self.n = n

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.i >= self.n:
            raise StopAsyncIteration
        self.i += 1
        return self.i


async def bad():
    yield 1
    raise KeyError("boom")


async def main():
    check("list", [x async for x in agen(4)], [0, 1, 2, 3])
    check("filtered", [x * 10 async for x in agen(6) if x % 2], [10, 30, 50])
    check("set", {x % 2 async for x in agen(5)}, {0, 1})
    check("dict", {x: x * x async for x in agen(3)}, {0: 0, 1: 1, 2: 4})
    check("empty", [x async for x in agen(0)], [])
    check("class iterator", [x async for x in ARange(3)], [1, 2, 3])
    check("nested", [(a, b) async for a in agen(2) for b in range(2)],
          [(0, 0), (0, 1), (1, 0), (1, 1)])
    try:
        [x async for x in bad()]
    except KeyError:
        pass
    else:
        raise AssertionError("an exception other than StopAsyncIteration was swallowed")
    return "done"


check("run", asyncio.run(main()), "done")
print("async_comprehension: ok")
