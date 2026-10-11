# Compiled by protopyc, exceptions, `with` statements and the methods of
# classes defined in functions behave as the interpreter runs the same source;
# protopyc_exceptions_with.sh compares the two outputs.

LOG = []


class Boom(Exception):
    pass


# --- A raising statement stops its statement list -------------------------

def stops_at_raise(out):
    out.append(1)
    raise ValueError("stop")
    out.append(2)


def caller_sees():
    out = []
    try:
        stops_at_raise(out)
        out.append("not reached")
    except ValueError as e:
        out.append("caught " + str(e))
    return out


def builtin_raises():
    out = []
    try:
        out.append("a")
        int("x")
        out.append("b")
    except ValueError:
        out.append("handler")
    try:
        d = {}
        out.append(d["missing"])
        out.append("c")
    except KeyError:
        out.append("key")
    return out


def fails():
    raise KeyError("k")


def argument_raises():
    out = []
    try:
        out.append(fails())
    except KeyError:
        out.append("no append ran")
    try:
        x = 1 + fails()
        out.append(x)
    except KeyError:
        out.append("no binding")
    return out


def raise_class():
    try:
        raise Boom
    except Boom as e:
        return type(e).__name__ + " " + str(isinstance(e, Exception))


def unmatched():
    try:
        raise ValueError("passes through")
    except KeyError:
        return "wrong handler"


def calls_unmatched():
    try:
        unmatched()
    except ValueError as e:
        return "outer got " + str(e)


def reraise():
    try:
        try:
            raise ValueError("again")
        except ValueError:
            LOG.append("inner handler")
            raise
    except ValueError as e:
        return "re-raised " + str(e)


def handler_raises():
    try:
        try:
            raise KeyError("first")
        except KeyError:
            raise TypeError("second")
    except TypeError as e:
        return "replaced by " + str(e)


def bad_test():
    raise RuntimeError("test")


def raising_tests():
    out = []
    try:
        if bad_test():
            out.append("then")
        else:
            out.append("else")
    except RuntimeError:
        out.append("if test")
    try:
        while bad_test():
            out.append("loop")
    except RuntimeError:
        out.append("while test")
    return out


# --- Loops -----------------------------------------------------------------

def in_loop(n):
    out = []
    for i in range(n):
        try:
            if i % 2:
                raise ValueError(i)
            out.append(i)
        except ValueError as e:
            out.append(-i)
    return out


def loop_stops(n):
    i = 0
    while i < n:
        LOG.append(i)
        if i == 3:
            raise RuntimeError("loop stopped at " + str(i))
        i += 1
    LOG.append("loop end")


def bad_iter():
    for v in [1, 2, 3]:
        if v == 2:
            raise IndexError("iteration")
        LOG.append(v)


# --- finally ---------------------------------------------------------------

def fin(flag):
    LOG.append("start")
    try:
        if flag:
            raise ValueError("v")
        LOG.append("body")
    finally:
        LOG.append("finally")
    LOG.append("after")


def call_fin(flag):
    try:
        fin(flag)
        return "no exception"
    except ValueError as e:
        return "propagated " + str(e)


def return_in_finally():
    try:
        raise ValueError("lost")
    finally:
        return "finally wins"


def return_through_finally():
    try:
        return "body value"
    finally:
        LOG.append("ran finally")


def finally_overrides_return():
    try:
        return "body"
    finally:
        return "finally"


def break_continue_finally():
    out = []
    for i in range(5):
        try:
            if i == 3:
                break
            if i == 0:
                continue
            out.append(i)
        finally:
            out.append("f" + str(i))
    return out


def nested(k):
    out = []
    try:
        try:
            if k == 1:
                raise KeyError("inner")
            if k == 2:
                raise ValueError("outer-only")
            out.append("no raise")
        except KeyError:
            out.append("inner handler")
            raise TypeError("from handler")
    except ValueError:
        out.append("outer ValueError")
    except TypeError as e:
        out.append("outer TypeError " + str(e))
    else:
        out.append("else")
    finally:
        out.append("outer finally")
    return out


def deep_return(n):
    for i in range(n):
        try:
            try:
                if i == 2:
                    return "returned at " + str(i)
            finally:
                LOG.append("inner " + str(i))
        finally:
            LOG.append("outer " + str(i))
    return "loop done"


# --- finally after an exception or a return leaves a handler --------------
# The interpreter does not run these finally clauses (it runs a try
# statement's finally clause only when the body or a handler completes
# normally); protopyc_exceptions_with.sh compares the compiled module's output
# on them with CPython's, recorded in exceptions_with_cpython.expected.

def return_in_handler():
    try:
        raise KeyError(1)
    except KeyError:
        return "handler value"
    finally:
        LOG.append("finally after return in handler")


def break_in_handler():
    for i in range(2):
        try:
            raise KeyError(i)
        except KeyError:
            break
        finally:
            LOG.append("finally after break in handler")
    return "broke"


def unmatched_with_finally():
    try:
        try:
            raise KeyError("k")
        except ValueError:
            LOG.append("wrong handler")
        finally:
            LOG.append("finally after unmatched")
    except KeyError as e:
        return "caught " + str(e)


def handler_raises_with_finally():
    try:
        try:
            raise KeyError("first")
        except KeyError:
            raise ValueError("second")
        finally:
            LOG.append("finally after handler raised")
    except ValueError as e:
        return "caught " + str(e)


def else_raises_with_finally():
    try:
        try:
            LOG.append("body")
        except KeyError:
            LOG.append("wrong handler")
        else:
            raise ValueError("from else")
        finally:
            LOG.append("finally after else raised")
    except ValueError as e:
        return "caught " + str(e)


# --- Methods ---------------------------------------------------------------

class Account:
    def __init__(self, balance):
        self.balance = balance

    def withdraw(self, amount):
        if amount > self.balance:
            raise ValueError("insufficient")
        self.balance = self.balance - amount
        return self.balance

    def try_withdraw(self, amount):
        try:
            return self.withdraw(amount)
        except ValueError as e:
            return "refused: " + str(e)


# --- with ------------------------------------------------------------------

class CM:
    def __init__(self, name, suppress=False):
        self.name = name
        self.suppress = suppress

    def __enter__(self):
        LOG.append("enter " + self.name)
        return self.name.upper()

    def __exit__(self, t, v, tb):
        if t is None:
            LOG.append("exit " + self.name + " clean " + str(v) + " " + str(tb))
        else:
            LOG.append("exit " + self.name + " " + t.__name__ + " " + str(v))
        return self.suppress


def with_basic():
    with CM("a") as x:
        LOG.append("body " + x)
    return "done"


def with_raise():
    with CM("b"):
        raise ValueError("in with")
    LOG.append("not reached")


def with_suppress():
    with CM("c", True):
        raise ValueError("suppressed")
        LOG.append("not reached")
    return "after suppress"


def with_nested():
    with CM("o") as a:
        with CM("i") as b:
            LOG.append(a + b)
            raise KeyError("nested")


def with_multi():
    with CM("m1") as a, CM("m2", True) as b:
        LOG.append(a + b)
        raise KeyError("multi")
    return "multi suppressed"


def with_return():
    with CM("r"):
        return "returned from with"


def with_loop():
    for i in range(3):
        with CM("l" + str(i)):
            if i == 0:
                continue
            if i == 1:
                break
    return "loop left"


class Truthy:
    def __enter__(self):
        return self

    def __exit__(self, t, v, tb):
        return 1


def with_truthy():
    with Truthy():
        raise ValueError("truthy suppresses")
    return "truthy ok"


# --- Methods of classes defined in functions -------------------------------

def make_greeter(prefix):
    calls = [0]

    class Greeter:
        def greet(self, name):
            calls[0] += 1
            return prefix + name + " #" + str(calls[0])

        def twice(self, name):
            return self.greet(name) + " | " + self.greet(name)

        def count(self):
            return calls[0]

    return Greeter


def make_loud(prefix):
    Base = make_greeter(prefix)
    suffix = "!"

    class Loud(Base):
        def greet(self, name):
            return Base.greet(self, name).upper() + suffix

    return Loud


def use_greeter():
    G = make_greeter("hello ")
    g = G()
    return g.twice("in") + " / " + str(g.count())


# --- Module level ----------------------------------------------------------

MOD = []
try:
    MOD.append(1)
    raise ValueError("module")
    MOD.append(2)
except ValueError as module_exc:
    MOD.append("module handler " + str(module_exc))
finally:
    MOD.append("module finally")
with CM("module"):
    MOD.append("module with")
