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
