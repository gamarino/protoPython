# An instance that stores a `__code__` of its own is not a function: calling
# it runs its class's __call__.  unittest.mock's AsyncMock stores a mock code
# object, and calling an AsyncMock returned None.  `await` of an object that
# is not awaitable raises TypeError instead of hanging.
import asyncio
from unittest.mock import AsyncMock


class C:
    async def run(self, x):
        return x * 2

    def __call__(self, x):
        return self.run(x)


class FakeCode:
    co_flags = 0x80


c = C()
c.__dict__["__code__"] = FakeCode()
coro = c(5)
assert type(coro).__name__ == "coroutine", type(coro)
assert asyncio.run(coro) == 10


async def addition(self, var):
    pass


async def use_mock():
    mock = AsyncMock(addition, return_value=10)
    assert await mock(5) == 10
    assert mock.await_count == 1


asyncio.run(use_mock())


async def await_none():
    await None


try:
    asyncio.run(await_none())
except TypeError as e:
    assert "can't be awaited" in str(e), e
else:
    raise AssertionError("await None did not raise TypeError")
print("callable_with_code_attr: ok")
