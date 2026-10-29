# Walking f_back from any frame ends in None.  The main module's f_back, and
# that of a frame whose caller runs without a frame object, were missing, so
# the walk raised AttributeError; asyncio in debug mode builds every task's source
# traceback that way, so IsolatedAsyncioTestCase (CPython's
# testmock/testasync.py) failed in every test.
import asyncio
import sys
import traceback


def depth_to_none(f):
    n = 0
    while f is not None:
        f = f.f_back
        n += 1
    return n


class Runner:
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def run(self):
        return depth_to_none(sys._getframe(0))


def inner():
    return depth_to_none(sys._getframe(0))


def outer():
    with Runner() as r:
        return r.run()


assert inner() >= 2
assert outer() >= 1
assert depth_to_none(sys._getframe(0)) >= 1
assert traceback.extract_stack()[-1].name == "<module>"


async def main():
    await asyncio.sleep(0)
    return depth_to_none(sys._getframe(0))


assert asyncio.run(main(), debug=True) >= 1

# asyncio's debug mode sets and reads the coroutine origin tracking depth.
assert sys.get_coroutine_origin_tracking_depth() == 0
sys.set_coroutine_origin_tracking_depth(3)
assert sys.get_coroutine_origin_tracking_depth() == 3
sys.set_coroutine_origin_tracking_depth(0)
try:
    sys.set_coroutine_origin_tracking_depth(-1)
except ValueError:
    pass
else:
    raise AssertionError("negative depth accepted")
print("frame_chain_ends_in_none: ok")
