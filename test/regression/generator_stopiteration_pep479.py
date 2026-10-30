# PEP 479: StopIteration escaping a generator body becomes RuntimeError.
import asyncio


def gen():
    yield 1
    raise StopIteration("inner")


g = gen()
assert next(g) == 1
try:
    next(g)
except RuntimeError as e:
    assert str(e) == "generator raised StopIteration", str(e)
    assert type(e.__cause__) is StopIteration and e.__cause__.args == ("inner",)
else:
    raise AssertionError("StopIteration escaped the generator")


def uses_next(it):
    yield next(it)


assert list(uses_next(iter([5]))) == [5]
try:
    list(uses_next(iter([])))
except RuntimeError:
    pass
else:
    raise AssertionError("next() on an exhausted iterator ended the loop silently")


def ret():
    yield 1
    return 7


r = ret()
next(r)
try:
    next(r)
except StopIteration as e:
    assert e.value == 7


def catches():
    try:
        yield 1
    except StopIteration:
        yield "caught"


c = catches()
next(c)
assert c.throw(StopIteration) == "caught"


async def coro():
    raise StopIteration


try:
    asyncio.run(coro())
except RuntimeError as e:
    assert "coroutine raised StopIteration" in str(e), str(e)
else:
    raise AssertionError("StopIteration escaped the coroutine")


async def agen():
    yield 1
    raise StopAsyncIteration


async def drain():
    return [x async for x in agen()]


try:
    asyncio.run(drain())
except RuntimeError as e:
    assert "async generator raised StopAsyncIteration" in str(e), str(e)
else:
    raise AssertionError("StopAsyncIteration escaped the async generator")
print("generator_stopiteration_pep479: ok")
