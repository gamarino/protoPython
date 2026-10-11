"""try / except / else / finally behave as in CPython.

A `finally` clause runs however the statement is left: normally, by an
exception from the body, from an `except` or `else` clause or one that no
`except` clause matches, and by `return`, `break` or `continue` from any
clause. A `return`, `break` or `continue` in the `finally` clause discards the
exception or return value in transit. An exception raised while another is
handled gets it as `__context__`; `raise X from Y` sets `__cause__`. Once the
statement is left, `sys.exc_info()` no longer reports the handled exception,
and an exception that passes through one frame has one traceback entry for it.

Every expected value below is what CPython 3.14 produces (the file runs under
CPython unchanged).
"""
import sys


class A(Exception):
    pass


class B(Exception):
    pass


class C(Exception):
    pass


def exc_name():
    e = sys.exc_info()[1]
    return None if e is None else type(e).__name__


# -- finally after a clause other than the body ------------------------------

def raise_in_except(log):
    try:
        try:
            raise A()
        except A:
            log.append("except")
            raise B()
        finally:
            log.append("finally")
    except B as e:
        log.append(("outer", type(e.__context__).__name__))


def no_handler_matches(log):
    try:
        try:
            raise C()
        except A:
            log.append("wrong handler")
        except B:
            log.append("wrong handler")
        finally:
            log.append("finally")
    except C:
        log.append("outer C")


def raise_in_else(log):
    try:
        try:
            log.append("body")
        except A:
            log.append("except")
        else:
            log.append("else")
            raise B()
        finally:
            log.append("finally")
    except B:
        log.append("outer B")


def _return_in_except(log):
    try:
        raise A()
    except A:
        log.append("except")
        return "from except"
    finally:
        log.append("finally")
    log.append("unreachable")


def return_in_except(log):
    log.append(_return_in_except(log))
    log.append(exc_name())


def _return_in_else(log):
    try:
        log.append("body")
    except A:
        pass
    else:
        return "from else"
    finally:
        log.append("finally")


def return_in_else(log):
    log.append(_return_in_else(log))


def break_continue_in_except(log):
    for i in range(4):
        try:
            raise A(i)
        except A:
            if i == 1:
                log.append(("continue", i))
                continue
            if i == 2:
                log.append(("break", i))
                break
            log.append(("handled", i))
        finally:
            log.append(("finally", i))
        log.append(("after", i))
    log.append(exc_name())


def break_continue_in_else(log):
    i = 0
    while i < 4:
        i += 1
        try:
            pass
        except A:
            pass
        else:
            if i == 2:
                continue
            if i == 3:
                break
        finally:
            log.append(("finally", i))
        log.append(("after", i))


# -- finally that leaves by return / break / continue ------------------------

def _return_overrides_return():
    try:
        return "try"
    finally:
        return "finally"


def _return_overrides_except_return():
    try:
        raise A()
    except A:
        return "except"
    finally:
        return "finally"


def _return_swallows_exception():
    try:
        raise A()
    finally:
        return "swallowed"


def _return_swallows_except_exception():
    try:
        raise A()
    except A:
        raise B()
    finally:
        return "swallowed B"


def return_overriding(log):
    log.append(_return_overrides_return())
    log.append(_return_overrides_except_return())
    log.append(_return_swallows_exception())
    log.append(_return_swallows_except_exception())
    log.append(exc_name())


def break_continue_in_finally(log):
    for i in range(3):
        try:
            raise A(i)
        finally:
            if i == 0:
                continue
            log.append(("finally", i))
            break
    log.append(("loop done", i))
    j = 0
    while j < 3:
        j += 1
        try:
            try:
                raise A(j)
            except A:
                raise B(j)
        finally:
            continue
    log.append(("while done", j))
    log.append(exc_name())


# -- nesting -----------------------------------------------------------------

def nested(log):
    try:
        try:
            try:
                raise A()
            except A:
                log.append("inner except")
                raise B()
            finally:
                log.append("inner finally")
        except B:
            log.append("middle except")
            raise C()
        finally:
            log.append("middle finally")
    except C as e:
        log.append(("outer", type(e.__context__).__name__,
                    type(e.__context__.__context__).__name__))
    log.append(exc_name())


def try_in_handler(log):
    try:
        raise A()
    except A:
        try:
            raise B()
        except B:
            log.append(("inner", exc_name()))
        log.append(("restored", exc_name()))
        try:
            raise
        except A:
            log.append("bare raise re-raised A")
    log.append(exc_name())


def finally_in_finally(log):
    def f():
        try:
            try:
                raise A()
            finally:
                log.append("inner finally")
                try:
                    raise B()
                finally:
                    log.append("innermost finally")
        finally:
            log.append("outer finally")
    try:
        f()
    except B as e:
        log.append(("caught B", type(e.__context__).__name__))


def raise_in_finally_replaces(log):
    try:
        try:
            raise A()
        finally:
            raise B()
    except B as e:
        log.append(("B context", type(e.__context__).__name__))
    except A:
        log.append("A escaped")


def loops_and_functions(log):
    def gen_values():
        out = []
        for i in range(3):
            try:
                if i == 1:
                    raise ValueError(i)
                out.append(i)
            except ValueError:
                out.append("v")
                continue
            finally:
                out.append("f")
        return out
    log.append(gen_values())


# -- exception chaining ------------------------------------------------------

def context_chaining(log):
    try:
        try:
            raise A("first")
        except A:
            1 / 0
    except ZeroDivisionError as e:
        log.append(("native", type(e.__context__).__name__))
    try:
        try:
            raise A("first")
        except A as a:
            raise B("second") from a
    except B as e:
        log.append(("cause", type(e.__cause__).__name__,
                    type(e.__context__).__name__, e.__suppress_context__))
    try:
        try:
            raise A()
        except A:
            raise B() from None
    except B as e:
        log.append(("from None", e.__cause__, e.__suppress_context__))
    try:
        raise A()
    except A as e:
        log.append(("no context", e.__context__))


# -- sys.exc_info() ----------------------------------------------------------

def exc_info_lifetime(log):
    try:
        raise A()
    except A:
        log.append(("in except", exc_name()))
    log.append(("after except", exc_name()))
    try:
        try:
            raise A()
        finally:
            log.append(("in finally", exc_name()))
    except A:
        pass
    log.append(("after finally", exc_name()))
    for i in range(2):
        try:
            raise A()
        except A:
            break
    log.append(("after break", exc_name()))


# -- generators --------------------------------------------------------------

def generator_close(log):
    def gen():
        try:
            yield 1
            yield 2
        finally:
            log.append("gen finally")
    g = gen()
    log.append(next(g))
    g.close()
    log.append("closed")

    def gen2():
        try:
            yield 1
        except GeneratorExit:
            log.append("GeneratorExit")
            raise
        finally:
            log.append("gen2 finally")
    g = gen2()
    next(g)
    g.close()

    def gen3():
        for i in range(3):
            try:
                yield i
            finally:
                log.append(("gen3 finally", i))
    log.append(list(gen3()))
    log.append(exc_name())


# -- with statements ---------------------------------------------------------

class Suppress:
    def __enter__(self):
        return self

    def __exit__(self, t, v, tb):
        return True


class Passthrough:
    def __init__(self, log):
        self.log = log

    def __enter__(self):
        return self

    def __exit__(self, t, v, tb):
        self.log.append(("exit", None if t is None else t.__name__))
        return False


def with_statements(log):
    with Suppress():
        raise A()
    log.append(("suppressed", exc_name()))
    try:
        with Passthrough(log):
            raise A()
    except A:
        log.append("propagated")
    log.append(exc_name())
    try:
        try:
            raise A()
        except A:
            with Passthrough(log):
                raise B()
    except B as e:
        log.append(("with in except", type(e.__context__).__name__))
    log.append(exc_name())


# -- tracebacks --------------------------------------------------------------

def _tb_len(e):
    n = 0
    tb = e.__traceback__
    while tb is not None:
        n += 1
        tb = tb.tb_next
    return n


def traceback_entries(log):
    def inner():
        try:
            try:
                raise A()
            except B:
                pass
            finally:
                pass
        except C:
            pass
    try:
        inner()
    except A as e:
        log.append(("through try/except/finally", _tb_len(e)))

    def reraise():
        try:
            raise A()
        except A:
            raise
    try:
        reraise()
    except A as e:
        log.append(("bare raise", _tb_len(e)))


CASES = [
    raise_in_except, no_handler_matches, raise_in_else, return_in_except,
    return_in_else, break_continue_in_except, break_continue_in_else,
    return_overriding, break_continue_in_finally, nested, try_in_handler,
    finally_in_finally, raise_in_finally_replaces, loops_and_functions,
    context_chaining, exc_info_lifetime, generator_close, with_statements,
    traceback_entries,
]

EXPECTED = {
    'raise_in_except': ['except', 'finally', ('outer', 'A')],
    'no_handler_matches': ['finally', 'outer C'],
    'raise_in_else': ['body', 'else', 'finally', 'outer B'],
    'return_in_except': ['except', 'finally', 'from except', None],
    'return_in_else': ['body', 'finally', 'from else'],
    'break_continue_in_except': [('handled', 0), ('finally', 0), ('after', 0), ('continue', 1), ('finally', 1), ('break', 2), ('finally', 2), None],
    'break_continue_in_else': [('finally', 1), ('after', 1), ('finally', 2), ('finally', 3)],
    'return_overriding': ['finally', 'finally', 'swallowed', 'swallowed B', None],
    'break_continue_in_finally': [('finally', 1), ('loop done', 1), ('while done', 3), None],
    'nested': ['inner except', 'inner finally', 'middle except', 'middle finally', ('outer', 'B', 'A'), None],
    'try_in_handler': [('inner', 'B'), ('restored', 'A'), 'bare raise re-raised A', None],
    'finally_in_finally': ['inner finally', 'innermost finally', 'outer finally', ('caught B', 'A')],
    'raise_in_finally_replaces': [('B context', 'A')],
    'loops_and_functions': [[0, 'f', 'v', 'f', 2, 'f']],
    'context_chaining': [('native', 'A'), ('cause', 'A', 'A', True), ('from None', None, True), ('no context', None)],
    'exc_info_lifetime': [('in except', 'A'), ('after except', None), ('in finally', 'A'), ('after finally', None), ('after break', None)],
    'generator_close': [1, 'gen finally', 'closed', 'GeneratorExit', 'gen2 finally', ('gen3 finally', 0), ('gen3 finally', 1), ('gen3 finally', 2), [0, 1, 2], None],
    'with_statements': [('suppressed', None), ('exit', 'A'), 'propagated', None, ('exit', 'B'), ('with in except', 'A'), None],
    'traceback_entries': [('through try/except/finally', 2), ('bare raise', 2)],
}


def main():
    failures = 0
    results = {}
    for case in CASES:
        log = []
        try:
            case(log)
        except BaseException as e:  # noqa: B902 -- report, do not stop
            log.append(("escaped", type(e).__name__))
        results[case.__name__] = log
        if case.__name__ in EXPECTED and log != EXPECTED[case.__name__]:
            failures += 1
            print("FAIL", case.__name__)
            print("  expected:", EXPECTED[case.__name__])
            print("  got:     ", log)
    if not EXPECTED:
        for name, log in results.items():
            print("    %r: %r," % (name, log))
        return 0
    print("%d/%d cases match CPython" % (len(CASES) - failures, len(CASES)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
