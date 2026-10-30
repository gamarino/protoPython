# gi_suspended (and cr_/ag_suspended) report a generator stopped at a
# yield; inspect.getgeneratorstate and getcoroutinestate read them.
import inspect


def gen():
    assert inspect.getgeneratorstate(g) == inspect.GEN_RUNNING
    yield 1
    yield 2


g = gen()
assert g.gi_suspended is False
assert inspect.getgeneratorstate(g) == inspect.GEN_CREATED
assert next(g) == 1
assert g.gi_suspended is True and not g.gi_running
assert inspect.getgeneratorstate(g) == inspect.GEN_SUSPENDED
assert next(g) == 2
assert list(g) == []
assert g.gi_suspended is False


def failing():
    yield 1
    raise ValueError


f = failing()
next(f)
try:
    next(f)
except ValueError:
    pass
assert f.gi_suspended is False

def plain():
    yield 1


h = plain()
next(h)
h.close()
assert h.gi_suspended is False


class Awaitable:
    def __await__(self):
        yield


async def coro():
    await Awaitable()


c = coro()
assert c.cr_suspended is False
assert inspect.getcoroutinestate(c) == inspect.CORO_CREATED
c.send(None)
assert c.cr_suspended is True
assert inspect.getcoroutinestate(c) == inspect.CORO_SUSPENDED
try:
    c.send(None)
except StopIteration:
    pass
assert c.cr_suspended is False
print("generator_suspended_state: ok")
