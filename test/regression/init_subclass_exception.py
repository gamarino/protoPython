# Regression test: an exception raised by __init_subclass__ (or the metaclass)
# during a class statement is catchable.
#
# BUILD_CLASS returned nullptr from the interpreter loop when __init_subclass__
# raised, bypassing every enclosing handler, so the module died instead of
# entering the except block.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class Picky:
    def __init_subclass__(cls, **kwargs):
        raise ValueError("no subclasses of " + cls.__name__)


try:
    class Child(Picky):
        pass
except ValueError as exc:
    message = str(exc)
else:
    message = None
check("__init_subclass__ exception caught", message, "no subclasses of Child")


class Meta(type):
    def __new__(mcls, name, bases, ns):
        raise KeyError(name)


try:
    class Rejected(metaclass=Meta):
        pass
except KeyError as exc:
    rejected = exc.args[0]
else:
    rejected = None
check("metaclass exception caught", rejected, "Rejected")


def build():
    class Inner(Picky):
        pass


try:
    build()
except ValueError:
    in_function = True
else:
    in_function = False
check("exception from a class statement inside a function", in_function, True)

print("init subclass exception OK")
