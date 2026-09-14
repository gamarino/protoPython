# Regression test: `import asyncio` works, and the runtime pieces
# asyncio.run() needs before it runs a coroutine exist.
#
# dataclasses asks typing.get_origin() about every field once typing is
# imported, and get_origin runs isinstance(tp, typing.Union).  The _typing
# stub's Union is an instance, not a class, so that isinstance raised
# TypeError and asyncio.graph (dataclasses) failed to import.  asyncio.run()
# then needed socket.socketpair() for the event loop's self-pipe (the _socket
# stub raised OSError), sys.is_finalizing(), sys.get/set_asyncgen_hooks()
# and signal.default_int_handler (missing) and a callable itertools.chain.
# asyncio.run() itself still fails later (loop.run_until_complete crashes).
import typing

assert isinstance(int | str, typing.Union)
assert not isinstance("x", typing.Union)
assert typing.get_origin("types.FrameType") is None
assert typing.get_origin(typing.Union[int, str]) is typing.Union

# asyncio.all_tasks() calls itertools.chain(), which was not callable.
import itertools

assert list(itertools.chain([1], (2, 3), "a")) == [1, 2, 3, "a"]
assert list(itertools.chain()) == []
assert list(itertools.chain.from_iterable([[1], [2]])) == [1, 2]

import asyncio
import asyncio.graph
import signal
import socket
import sys

assert callable(signal.default_int_handler)

assert sys.is_finalizing() is False
hooks = sys.get_asyncgen_hooks()
assert tuple(hooks) == (None, None), hooks
sys.set_asyncgen_hooks(firstiter=print, finalizer=None)
assert sys.get_asyncgen_hooks()[0] is print
sys.set_asyncgen_hooks(*hooks)
assert tuple(sys.get_asyncgen_hooks()) == (None, None)

a, b = socket.socketpair()
b.send(b"ping")
assert a.recv(10) == b"ping"
a.sendall(b"pong")
assert b.recv(10) == b"pong"
a.setblocking(False)
try:
    a.recv(10)
except BlockingIOError:
    pass
else:
    raise AssertionError("an empty non-blocking recv must raise BlockingIOError")
a.close()
b.close()

print("asyncio run basic OK")
