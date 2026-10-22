# Paths through which an exception no handler selects must leave unchanged
# (protoCore's rule E3): the C++ exception object that entered, a runtime's
# proto::ProtoException subclass with its extras, not a new exception with
# the same value.  test_exception_identity (test/exceptions/) calls each
# function with `raiser`, a native method that throws such a subclass, and
# checks what reaches C++.  It runs this module interpreted and compiled by
# protopyc.


def unmatched_except(raiser):
    try:
        raiser()
    except KeyError:
        pass
    except (ValueError, TypeError):
        pass


def nested_unmatched(raiser):
    try:
        try:
            raiser()
        except IndexError:
            pass
    except LookupError:
        pass


def through_finally(raiser):
    ran = []
    try:
        raiser()
    finally:
        ran.append(1)


def bare_raise(raiser):
    try:
        raiser()
    except BaseException:
        raise


def bare_raise_after_nested(raiser):
    try:
        raiser()
    except Exception:
        try:
            {}["missing"]
        except KeyError:
            pass
        raise


class _Recorder:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, tb):
        return False


def through_with(raiser):
    with _Recorder():
        raiser()


def _generator(raiser):
    yield 1
    raiser()
    yield 2


def through_generator(raiser):
    for _ in _generator(raiser):
        pass


def _inner_generator(raiser):
    yield 1
    try:
        raiser()
    finally:
        pass


def through_yield_from(raiser):
    def outer():
        yield from _inner_generator(raiser)
    for _ in outer():
        pass


def through_calls(raiser):
    def level(n):
        if n == 0:
            raiser()
        try:
            level(n - 1)
        except ArithmeticError:
            pass
    level(3)


CASES = ["unmatched_except", "nested_unmatched", "through_finally", "bare_raise",
         "bare_raise_after_nested", "through_with", "through_generator",
         "through_yield_from", "through_calls"]
