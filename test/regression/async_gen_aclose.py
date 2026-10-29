# `await agen.aclose()` closes an async generator: aclose() returns an
# awaitable (CPython).  It used to be generator.close() itself, returning
# None, and awaiting it never finished (test_asyncgen timed out at
# test_async_gen_aclose_after_exhaustion).
import asyncio

log = []


async def numbers():
    try:
        yield 1
        yield 2
    finally:
        log.append("finally")


async def main():
    it = numbers()
    async for _ in it:          # exhausted
        pass
    assert await it.aclose() is None
    assert log == ["finally"], log

    it = numbers()
    assert await it.__anext__() == 1   # suspended at the first yield
    aw = it.aclose()
    assert hasattr(aw, "__await__")
    await aw
    assert log == ["finally", "finally"], log
    try:
        await it.__anext__()
    except StopAsyncIteration:
        pass
    else:
        raise AssertionError("a closed async generator produced a value")
    await it.aclose()                   # closing twice is harmless

    it = numbers()
    assert await it.asend(None) == 1 and await it.asend(None) == 2
    await it.aclose()
    assert log == ["finally"] * 3, log

    await numbers().aclose()            # never started: nothing runs
    assert log == ["finally"] * 3, log
    return "done"


assert asyncio.run(main()) == "done"
print("async_gen_aclose: ok")
