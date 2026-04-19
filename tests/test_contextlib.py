"""Conformance test for the contextlib module."""
import contextlib

# contextmanager decorator
@contextlib.contextmanager
def managed(value):
    yield value * 2

with managed(5) as v:
    assert v == 10

# contextmanager with exception suppression
@contextlib.contextmanager
def suppressor():
    try:
        yield
    except ValueError:
        pass

with suppressor():
    raise ValueError('should be suppressed')

# suppress
with contextlib.suppress(ValueError):
    raise ValueError('suppressed')

with contextlib.suppress(TypeError, ValueError):
    raise TypeError('also suppressed')

# suppress without exception
with contextlib.suppress(ValueError):
    x = 1 + 1

# closing
class Resource:
    def __init__(self):
        self.closed = False
    def close(self):
        self.closed = True

r = Resource()
with contextlib.closing(r) as res:
    assert res is r
    assert not res.closed
assert r.closed

# nullcontext
with contextlib.nullcontext(42) as v:
    assert v == 42

with contextlib.nullcontext() as v:
    assert v is None

# ExitStack with callbacks (LIFO order)
log = []
with contextlib.ExitStack() as stack:
    stack.callback(log.append, 1)
    stack.callback(log.append, 2)
    stack.callback(log.append, 3)
assert log == [3, 2, 1]

# AbstractContextManager
class MyCtx(contextlib.AbstractContextManager):
    def __init__(self):
        self.entered = False
        self.exited = False
    def __enter__(self):
        self.entered = True
        return self
    def __exit__(self, *args):
        self.exited = True
        return False

ctx = MyCtx()
with ctx as m:
    assert m is ctx
    assert ctx.entered
assert ctx.exited

# Module has expected names
for name in ['contextmanager', 'suppress', 'closing', 'nullcontext',
             'ExitStack', 'AbstractContextManager',
             'AbstractAsyncContextManager']:
    assert hasattr(contextlib, name), f"missing: {name}"

print("test_contextlib passed")
