# Regression test: asyncio.run() drives simple programs to completion.
#
# asyncio.gather() hung forever: its _done_callback counts finished children
# with `nonlocal nfinished`, and the nonlocal rebinding never reached
# gather's own frame (see nonlocal_rebinding.py), so `nfinished == nfuts`
# never held and the outer future was never resolved.
import asyncio


async def returns_value():
    return 42

assert asyncio.run(returns_value()) == 42


async def sleeps():
    await asyncio.sleep(0)
    await asyncio.sleep(0.01)
    return "slept"

assert asyncio.run(sleeps()) == "slept"


async def double(n):
    await asyncio.sleep(0)
    return n * 2

async def gathers():
    return await asyncio.gather(double(1), double(2), double(3))

assert asyncio.run(gathers()) == [2, 4, 6]


async def gathers_eager():
    # Children that finish without suspending complete gather eagerly.
    async def plain(n):
        return n
    return await asyncio.gather(plain(1), plain(2))

assert asyncio.run(gathers_eager()) == [1, 2]


async def creates_task():
    task = asyncio.create_task(double(21))
    other = asyncio.create_task(double(5))
    return await task, await other

assert asyncio.run(creates_task()) == (42, 10)


async def raises():
    await asyncio.sleep(0)
    raise ValueError("boom")

try:
    asyncio.run(raises())
except ValueError as exc:
    assert str(exc) == "boom"
else:
    raise AssertionError("the exception must propagate out of asyncio.run")


async def gather_exception():
    async def bad():
        await asyncio.sleep(0)
        raise KeyError("child")
    return await asyncio.gather(double(1), bad())

try:
    asyncio.run(gather_exception())
except KeyError:
    pass
else:
    raise AssertionError("a child's exception must propagate out of gather")


async def gather_return_exceptions():
    async def bad():
        raise RuntimeError("kept")
    return await asyncio.gather(double(2), bad(), return_exceptions=True)

res = asyncio.run(gather_return_exceptions())
assert res[0] == 4 and isinstance(res[1], RuntimeError), res

loop = asyncio.new_event_loop()
try:
    assert loop.run_until_complete(returns_value()) == 42
finally:
    loop.close()

print("asyncio run programs OK")
