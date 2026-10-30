# agen.athrow() returns an awaitable that throws when driven, as in CPython.
# It used to throw at once and return what the generator yielded, so
# `await agen.athrow(E)` awaited that value (test_asyncgen hung on a
# yielded StopIteration).
import asyncio
import contextlib


async def gen():
    try:
        yield 1
    except ZeroDivisionError:
        yield StopIteration(2)


async def ends():
    try:
        yield 1
    except ValueError:
        return


async def main():
    g = gen()
    assert await g.asend(None) == 1
    aw = g.athrow(ZeroDivisionError)
    assert not isinstance(aw, StopIteration)   # nothing ran yet
    v = await aw
    assert isinstance(v, StopIteration) and v.value == 2, v
    try:
        await g.asend(None)
    except StopAsyncIteration:
        pass
    else:
        raise AssertionError("generator not exhausted")

    g = ends()
    await g.asend(None)
    try:
        await g.athrow(ValueError)
    except StopAsyncIteration:
        pass
    else:
        raise AssertionError("athrow into a returning generator")

    g = gen()
    await g.asend(None)
    try:
        await g.athrow(KeyError("k"))
    except KeyError:
        pass
    else:
        raise AssertionError("athrow did not propagate")

    # asynccontextmanager throws the body's exception with athrow.
    @contextlib.asynccontextmanager
    async def cm():
        try:
            yield
        except RuntimeError:
            pass

    async with cm():
        raise RuntimeError("swallowed")
    return "done"


assert asyncio.run(main()) == "done"
print("async_gen_athrow: ok")
