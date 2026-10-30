# Code flags use CPython's numbering: CO_COROUTINE 0x80, CO_ITERABLE_COROUTINE
# 0x100, CO_ASYNC_GENERATOR 0x200 (async generators do not carry
# CO_GENERATOR).  protoPython had shifted them (coroutine 0x100, async
# generator 0x100|0x20), so inspect.isasyncgenfunction was False and
# types.coroutine's "| 0x100" marked a generator as an async generator.
import asyncio
import inspect
import types

assert (inspect.CO_COROUTINE, inspect.CO_ITERABLE_COROUTINE,
        inspect.CO_ASYNC_GENERATOR) == (0x80, 0x100, 0x200)


async def coro():
    return 1


async def agen():
    yield 1


def gen():
    yield 1


assert coro.__code__.co_flags & 0x80 and not coro.__code__.co_flags & 0x220
assert agen.__code__.co_flags & 0x200 and not agen.__code__.co_flags & 0xA0
assert gen.__code__.co_flags & 0x20 and not gen.__code__.co_flags & 0x380
assert inspect.iscoroutinefunction(coro) and not inspect.isasyncgenfunction(coro)
assert inspect.isasyncgenfunction(agen) and not inspect.iscoroutinefunction(agen)
assert inspect.isgeneratorfunction(gen) and not inspect.isgeneratorfunction(agen)

c = coro()
assert inspect.iscoroutine(c)
try:
    c.send(None)
except StopIteration as e:
    assert e.value == 1


async def collect():
    return [x async for x in agen()]


assert asyncio.run(collect()) == [1]


@types.coroutine
def legacy():
    yield


async def use_legacy():
    await legacy()
    return 2


assert asyncio.run(use_legacy()) == 2
assert legacy.__code__.co_flags & 0x120 == 0x120  # types.coroutine's replace()
r = gen.__code__.replace(co_name="renamed", co_flags=gen.__code__.co_flags | 0x100)
assert r.co_name == "renamed" and r.co_flags & 0x100 and gen.__code__.co_name == "gen"
for bad in ({"co_code": b""}, {"co_bogus": 1}):
    try:
        gen.__code__.replace(**bad)
    except (ValueError, TypeError):
        pass
    else:
        raise AssertionError("replace accepted %r" % (bad,))
print("code_flags_cpython: ok")
