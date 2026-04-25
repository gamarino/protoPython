"""
Synthetic generator/coroutine baseline (PA-round prep).

Each test_* function exercises one concrete generator semantic.  Pure
asserts; no CPython infrastructure (test.support, doctest) needed.

Goal: a stable, machine-checkable target for the upcoming
generators-and-async cleanup.  Run before AND after any change to
OP_YIELD*, py_generator_*, runUserFunctionCall's generator branch, or
related ExecutionEngine code, and diff the PASS/FAIL columns.

A test is PASS when its function runs to completion (asserts hold).
A test is FAIL when an assert fails — captured per-test, the suite keeps
going.  A test is CRASH when an exception escapes the test function
(also captured; the suite continues).
"""


_ran = []


def _run(fn):
    name = fn.__name__
    try:
        fn()
        _ran.append((name, "PASS", None))
    except AssertionError as e:
        _ran.append((name, "FAIL", str(e) or "assertion"))
    except BaseException as e:
        _ran.append((name, "CRASH", "%s: %s" % (type(e).__name__, e)))
    return fn


# --- Basic yield / iter --------------------------------------------------


@_run
def test_basic_yield():
    def g():
        yield 1
        yield 2
        yield 3
    out = []
    for x in g():
        out.append(x)
    assert out == [1, 2, 3], out


@_run
def test_next_call():
    def g():
        yield 1
        yield 2
    it = g()
    assert next(it) == 1
    assert next(it) == 2
    raised = False
    try:
        next(it)
    except StopIteration:
        raised = True
    assert raised


@_run
def test_send_value():
    """A value sent into a generator becomes the result of the
    suspending `yield` expression."""
    def g():
        x = yield 1
        y = yield x + 10
        yield y * 100
    it = g()
    assert next(it) == 1            # first yield emits 1
    assert it.send(5) == 15         # x = 5; second yield emits 5+10
    assert it.send(7) == 700        # y = 7; third yield emits 7*100


@_run
def test_send_to_just_started_must_be_none():
    """First .send() on a fresh generator must accept only None."""
    def g():
        yield 1
    it = g()
    raised = False
    try:
        it.send("not none")
    except TypeError:
        raised = True
    assert raised, "send(non-None) on fresh generator must raise TypeError"


# --- yield from ---------------------------------------------------------


@_run
def test_yield_from_basic():
    def inner():
        yield "a"
        yield "b"
    def outer():
        yield from inner()
        yield "c"
    assert list(outer()) == ["a", "b", "c"]


@_run
def test_yield_from_returns_value():
    """`x = yield from g()` binds x to g's StopIteration value."""
    def inner():
        yield 1
        return 42
    def outer():
        x = yield from inner()
        yield ("done", x)
    assert list(outer()) == [1, ("done", 42)]


@_run
def test_yield_from_send_threaded():
    """A value sent to the outer generator is delivered to the inner one."""
    def inner():
        v = yield "i1"
        yield ("inner saw", v)
    def outer():
        yield from inner()
    it = outer()
    assert next(it) == "i1"
    assert it.send("hello") == ("inner saw", "hello")


@_run
def test_yield_from_iterator():
    """yield from accepts a plain iterator, not just a generator."""
    def outer():
        yield from [10, 20, 30]
        yield "tail"
    assert list(outer()) == [10, 20, 30, "tail"]


@_run
def test_yield_from_nested():
    def innermost():
        yield 1
        yield 2
    def middle():
        yield from innermost()
        yield 3
    def outer():
        yield from middle()
        yield 4
    assert list(outer()) == [1, 2, 3, 4]


# --- StopIteration value ------------------------------------------------


@_run
def test_return_value_in_generator():
    """`return v` inside a generator stores v on StopIteration."""
    def g():
        yield 1
        return 99
    it = g()
    assert next(it) == 1
    try:
        next(it)
        assert False, "expected StopIteration"
    except StopIteration as e:
        assert e.value == 99, e.value


# --- close / GeneratorExit ----------------------------------------------


@_run
def test_close_raises_generator_exit():
    seen = []
    def g():
        try:
            yield 1
            yield 2  # never reached
        except GeneratorExit:
            seen.append("exit")
            raise  # important: must propagate
    it = g()
    next(it)
    it.close()
    assert seen == ["exit"]


@_run
def test_close_swallowing_exit_is_silent():
    """If the generator catches GeneratorExit and returns, close() is silent."""
    def g():
        try:
            yield 1
        except GeneratorExit:
            return  # swallow — allowed
    it = g()
    next(it)
    it.close()  # must not raise


@_run
def test_close_yields_after_exit_is_runtime_error():
    """If the generator yields a new value after catching GeneratorExit,
    close() must raise RuntimeError."""
    def g():
        try:
            yield 1
        except GeneratorExit:
            yield "still going"
    it = g()
    next(it)
    raised = False
    try:
        it.close()
    except RuntimeError:
        raised = True
    assert raised, "yield-after-GeneratorExit must raise RuntimeError"


# --- throw -------------------------------------------------------------


@_run
def test_throw_caught():
    def g():
        try:
            yield 1
        except ValueError:
            yield "caught"
    it = g()
    assert next(it) == 1
    assert it.throw(ValueError("x")) == "caught"


@_run
def test_throw_propagates():
    def g():
        yield 1
    it = g()
    next(it)
    raised = False
    try:
        it.throw(ValueError("uncaught"))
    except ValueError as e:
        raised = True
        assert str(e) == "uncaught"
    assert raised


@_run
def test_throw_inside_yield_from():
    """throw at outer should propagate into the active subiter."""
    def inner():
        try:
            yield "i1"
        except ValueError as e:
            yield ("inner caught", str(e))
    def outer():
        yield from inner()
    it = outer()
    assert next(it) == "i1"
    assert it.throw(ValueError("bang")) == ("inner caught", "bang")


# --- Iteration after close / exhaustion ---------------------------------


@_run
def test_iter_after_close_raises_stopiteration():
    def g():
        yield 1
    it = g()
    it.close()
    raised = False
    try:
        next(it)
    except StopIteration:
        raised = True
    assert raised


@_run
def test_iter_after_exhaustion_raises_stopiteration():
    def g():
        yield 1
    it = g()
    next(it)
    try:
        next(it)
    except StopIteration:
        pass
    raised = False
    try:
        next(it)
    except StopIteration:
        raised = True
    assert raised


# --- try/finally inside generator ---------------------------------------


@_run
def test_finally_runs_on_close():
    seen = []
    def g():
        try:
            yield 1
        finally:
            seen.append("f")
    it = g()
    next(it)
    it.close()
    assert seen == ["f"]


@_run
def test_finally_runs_on_exhaustion():
    seen = []
    def g():
        try:
            yield 1
        finally:
            seen.append("f")
    list(g())
    assert seen == ["f"]


# --- Coroutine basics (no extra plumbing) -------------------------------


@_run
def test_async_def_returns_object():
    """`async def` invocation returns *something* (coroutine-shaped)."""
    async def coro():
        return 42
    c = coro()
    assert c is not None


@_run
def test_async_def_send_returns_value_via_stopiteration():
    async def coro():
        return 42
    c = coro()
    try:
        c.send(None)
        assert False, "coroutine should StopIteration immediately"
    except StopIteration as e:
        assert e.value == 42, e.value


# --- Async iterator pieces (just the protocol shape) --------------------


@_run
def test_async_iter_protocol():
    class A:
        def __init__(self):
            self.i = 0
        def __aiter__(self):
            return self
        async def __anext__(self):
            if self.i >= 2:
                raise StopAsyncIteration
            self.i += 1
            return self.i
    a = A()
    assert a.__aiter__() is a
    # Drive __anext__ once via .send(None).
    n = a.__anext__()
    try:
        n.send(None)
        assert False, "should StopIteration with value 1"
    except StopIteration as e:
        assert e.value == 1, e.value


# --- Async generator type identity --------------------------------------


@_run
def test_async_generator_is_distinct_from_sync():
    def sync():
        yield 1
    async def asyncgen():
        yield 1
    s = sync()
    a = asyncgen()
    # We don't yet require type(a).__name__ == 'async_generator',
    # but they must not be the *same* object identity nor share
    # all attrs identically — at minimum, an async generator should
    # expose __aiter__ while a sync generator does not.
    assert hasattr(a, "__aiter__"), "async generator must expose __aiter__"
    assert not hasattr(s, "__aiter__"), "sync generator must NOT expose __aiter__"


# --- PC-round: async for / async with / async generator methods --------
#
# These exercise the full async iteration contract.  All driven by a
# tiny synchronous "event loop" (`run`) that pumps a coroutine via
# .send(None) until StopIteration.


def run(coro, max_steps=10000):
    """Drive a coroutine to completion; return its return value.

    `max_steps` guards against bugs in async iteration that would
    otherwise hang the whole suite — an individual test failing this
    way is recorded as CRASH("max_steps exceeded") instead.
    """
    try:
        for _ in range(max_steps):
            coro.send(None)
        raise RuntimeError("max_steps exceeded — likely infinite async loop")
    except StopIteration as e:
        return e.value


# NOTE: tests below currently hang the runtime in an unbreakable C++
# loop when `async for` drives a built-in async generator.  Body kept
# as plain `def` skeletons (decorated to FAIL with a known message)
# so the suite still runs to completion.  To re-enable, change `def`
# to `async def`, restore the @_run decorator above each, and add the
# corresponding `async for / await` body.

@_run
def test_async_for_basic():
    raise AssertionError("disabled: async-for-over-async-gen hangs runtime; PC fix pending")


@_run
def test_async_for_break():
    raise AssertionError("disabled: async-for hangs runtime; PC fix pending")


@_run
def test_async_for_else():
    raise AssertionError("disabled: async-for hangs runtime; PC fix pending")


class _SyntheticAIter:
    def __init__(self, n):
        self.i = 0
        self.n = n
    def __aiter__(self):
        return self
    async def __anext__(self):
        if self.i >= self.n:
            raise StopAsyncIteration
        self.i += 1
        return self.i


@_run
def test_async_for_over_class_aiter():
    raise AssertionError("disabled: async-for hangs runtime; PC fix pending")


class _SyntheticMgr:
    def __init__(self, log):
        self.log = log
    async def __aenter__(self):
        self.log.append("enter")
        return "value"
    async def __aexit__(self, exc_type, exc, tb):
        self.log.append(("exit", exc_type.__name__ if exc_type else None))
        return False


@_run
def test_async_with_enter_exit_called():
    raise AssertionError("disabled: async-with hangs runtime; PC fix pending")


class _SyntheticMgrPropagating:
    def __init__(self, log):
        self.log = log
    async def __aenter__(self):
        return None
    async def __aexit__(self, exc_type, exc, tb):
        self.log.append(exc_type.__name__ if exc_type else None)
        return False


@_run
def test_async_with_exception_seen_by_aexit():
    raise AssertionError("disabled: async-with hangs runtime; PC fix pending")


class _SyntheticMgrSuppressing:
    async def __aenter__(self):
        return None
    async def __aexit__(self, exc_type, exc, tb):
        return True  # suppress


@_run
def test_async_with_suppression():
    raise AssertionError("disabled: async-with hangs runtime; PC fix pending")


# --- Async generator methods: asend / athrow / aclose --------------------


@_run
def test_async_generator_has_asend():
    async def agen():
        yield 1
    a = agen()
    assert hasattr(a, "asend"), "async_generator must expose asend"


@_run
def test_async_generator_has_athrow_aclose():
    async def agen():
        yield 1
    a = agen()
    assert hasattr(a, "athrow"), "async_generator must expose athrow"
    assert hasattr(a, "aclose"), "async_generator must expose aclose"


@_run
def test_asend_drives_one_step():
    """agen.asend(v) returns a coroutine that, when run, advances agen."""
    async def agen():
        x = yield 1
        yield x + 1
    a = agen()
    first = run(a.asend(None))     # gets the 1
    assert first == 1
    second = run(a.asend(10))      # x = 10; yields 11
    assert second == 11


# --- await chain --------------------------------------------------------


@_run
def test_await_returns_value():
    """`x = await coro()` binds the awaited coroutine's return value."""
    async def inner():
        return 42
    async def outer():
        x = await inner()
        return x + 1
    assert run(outer()) == 43


@_run
def test_await_chain_three_levels():
    async def a():
        return 1
    async def b():
        x = await a()
        return x + 10
    async def c():
        y = await b()
        return y + 100
    assert run(c()) == 111


# --- yield from inside async def is forbidden (PEP 525) -----------------


@_run
def test_yield_from_inside_async_def_is_syntax_error():
    src = "async def f():\n    yield from [1, 2]\n"
    raised = False
    try:
        compile(src, "<test>", "exec")
    except SyntaxError:
        raised = True
    assert raised, "yield from inside async def must be SyntaxError"


# --- Reporting ----------------------------------------------------------


def main():
    npass = sum(1 for _, st, _ in _ran if st == "PASS")
    nfail = sum(1 for _, st, _ in _ran if st == "FAIL")
    ncrash = sum(1 for _, st, _ in _ran if st == "CRASH")
    print("=" * 60)
    for name, status, detail in _ran:
        if status == "PASS":
            print("PASS  %s" % name)
        else:
            print("%-5s %s  (%s)" % (status, name, detail))
    print("=" * 60)
    print("PASS=%d  FAIL=%d  CRASH=%d  TOTAL=%d" %
          (npass, nfail, ncrash, len(_ran)))
    # Exit non-zero only if anything other than PASS occurred — useful
    # for CI / regression checks.
    import sys
    sys.exit(0 if (nfail == 0 and ncrash == 0) else 1)


main()
