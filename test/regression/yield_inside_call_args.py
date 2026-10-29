# A generator or coroutine that suspends while a call is being built keeps
# the call's callable.  The NULL that PUSH_NULL / LOAD_GLOBAL put on the
# operand stack below the callable was lost when the suspended stack was
# stored, so `f(1, (yield))`, `print("x", await y)` and
# `r.append(await y)` raised "'NoneType' object is not callable" on resume.
import asyncio


def f(*a):
    return a


def drive(gen):
    g = gen()
    next(g)
    try:
        g.send(5)
    except StopIteration as e:
        return e.value
    raise AssertionError("generator did not finish")


def global_callee():
    return f(1, (yield))


def local_callee():
    x = f
    return x((yield), 2)


class K:
    def m(self, *a):
        return a


def method_callee():
    return K().m((yield), 3)


assert drive(global_callee) == (1, 5)
assert drive(local_callee) == (5, 2)
assert drive(method_callee) == (5, 3)


async def main():
    r = []
    r.append(await asyncio.sleep(0, 7))
    assert r == [7]
    assert f("x", await asyncio.sleep(0.001, 8)) == ("x", 8)
    # A comprehension that awaits is a coroutine the function awaits.
    assert [await asyncio.sleep(0, i) for i in range(3)] == [0, 1, 2]
    assert {await asyncio.sleep(0, i) for i in range(3)} == {0, 1, 2}
    assert {i: await asyncio.sleep(0, i * 2) for i in range(2)} == {0: 0, 1: 2}
    assert [i for i in range(4) if await asyncio.sleep(0, i % 2)] == [1, 3]
    assert [j for i in range(2) for j in await asyncio.sleep(0, [i, i])] == [0, 0, 1, 1]
    return "done"


assert asyncio.run(main()) == "done"
print("yield_inside_call_args: ok")
