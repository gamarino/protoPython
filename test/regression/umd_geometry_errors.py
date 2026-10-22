# Errors of a module written in C++, caught by Python handlers.
#
# protoCore's example geometry module (protoCore/examples/exceptions/, a UMD
# provider in its own shared library, logical path "example/geometry") defines
# its own error types -- GeometryError, with NegativeRadius and
# DegenerateTriangle below it -- and raises them as proto::ProtoExceptions.
# They are not Python classes: a Python `except` that names one selects the
# error by its prototype chain, as every language on protoCore does
# (docs/EXCEPTIONS.md).  protoCore's own errors arrive as Python's built-in
# exceptions through protoPython's error factory, and a C++ std::exception as
# RuntimeError(what()).
#
# Run by protopy_umd_host (test/umd/), which registers the module's provider;
# protopyc_umd_geometry compiles this file with protopyc and runs it again.
import sys
import example.geometry as geometry
from example import geometry as geometry_again

failures = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


NEGATIVE = "circleArea: the radius -1.000000 is negative"

check(geometry_again is geometry, "`from example import geometry` is the same module")
check(sys.modules["example.geometry"] is geometry, "the module is in sys.modules")

# The module's own type catches its error, with the module's message.
caught = None
try:
    geometry.circleArea(-1)
except geometry.NegativeRadius as e:
    caught = e
check(caught is not None, "except NegativeRadius catches circleArea(-1)")
check(caught is not None and caught.message == NEGATIVE, "e.message is the module's message")
check(caught is not None and caught.name == "NegativeRadius", "e.name is inherited from the type")
check(caught is not None and str(caught) == NEGATIVE, "str(e) is the message")
check(caught is not None and repr(caught) == "NegativeRadius(" + repr(NEGATIVE) + ")",
      "repr(e) is NegativeRadius('<message>')")
check(type(caught) is geometry.NegativeRadius, "type(e) is the module's type")
import io
printed = io.StringIO()
print(caught, file=printed)
check(printed.getvalue() == NEGATIVE + "\n", "print(e) prints the message (" + repr(printed.getvalue()) + ")")
check(f"{caught}" == NEGATIVE, "f'{e}' is the message")

# The module's base type catches it too.
caught = None
try:
    geometry.circleArea(-1)
except geometry.GeometryError as e:
    caught = e
check(caught is not None and caught.message == NEGATIVE, "except GeometryError catches NegativeRadius")

# A handler for an unrelated type does not run; the error passes it unchanged.
where = None
try:
    try:
        geometry.circleArea(-1)
    except geometry.DegenerateTriangle:
        where = "the unrelated module handler"
    except ValueError:
        where = "a Python class that is not a match"
except geometry.GeometryError:
    where = "the outer handler"
check(where == "the outer handler", "unrelated handlers do not catch it (" + str(where) + ")")

# A tuple of types, one of them the module's.
where = None
try:
    geometry.circleArea(-1)
except (ZeroDivisionError, geometry.GeometryError):
    where = "tuple"
check(where == "tuple", "a tuple naming the module's base type catches it")

# The other error type of the module.
caught = None
try:
    geometry.triangleArea(1, 2, 10)
except geometry.DegenerateTriangle as e:
    caught = e
check(caught is not None and caught.message == "triangleArea: the sides do not make a triangle",
      "triangleArea(1, 2, 10) raises DegenerateTriangle")

# protoCore's malformed-integer error, raised inside the module, is Python's
# ValueError: the space's error factory built it.
caught = None
try:
    geometry.parseRadius("ten")
except ValueError as e:
    caught = e
check(type(caught) is ValueError, "parseRadius('ten') raises ValueError (" + repr(caught) + ")")
check(caught is not None and len(caught.args) == 1 and isinstance(caught.args[0], str),
      "the factory's ValueError has args == (message,)")

# Integer division by zero in Python is Python's own ZeroDivisionError.
for expr in ("1 // 0", "1 % 0", "divmod(7, 0)", "(10 ** 40) // 0", "(10 ** 40) % 0"):
    caught = None
    try:
        eval(expr)
    except ZeroDivisionError as e:
        caught = e
    check(type(caught) is ZeroDivisionError, expr + " raises ZeroDivisionError")
caught = None
try:
    1 // 0
except ArithmeticError as e:
    caught = e
check(caught is not None and str(caught) == "division by zero",
      "1 // 0 has CPython 3.14's message (" + str(caught) + ")")

# A C++ std::runtime_error is a module defect: Python's RuntimeError(what()).
caught = None
try:
    geometry.legacyArea(-1)
except RuntimeError as e:
    caught = e
check(type(caught) is RuntimeError and str(caught) == "legacyArea: negative radius",
      "legacyArea(-1) raises RuntimeError('legacyArea: negative radius') (" + repr(caught) + ")")

# A foreign error is an ordinary error to Python code: except Exception,
# except BaseException and a bare except catch it.
for clause in ("Exception", "BaseException", ""):
    caught = None
    src = ("try:\n    geometry.circleArea(-2)\nexcept " + clause + ":\n"
           "    caught = sys.exc_info()[1]\n")
    exec(src)
    check(caught is not None and caught.name == "NegativeRadius",
          "except " + (clause or "(bare)") + " catches the module's error")

# sys.exc_info() and a bare raise see the foreign value itself.
info = None
try:
    try:
        geometry.circleArea(-1)
    except geometry.GeometryError:
        info = sys.exc_info()
        raise
except geometry.NegativeRadius as e:
    check(info is not None and info[1] is e, "a bare raise re-raises the same value")

# A Python exception raised while handling one has it as __context__; the
# foreign value itself is never written to.
caught = None
try:
    try:
        geometry.circleArea(-1)
    except geometry.GeometryError as inner:
        raise KeyError("while handling")
except KeyError as e:
    caught = e
check(caught is not None and caught.__context__ is not None and caught.__context__.name == "NegativeRadius",
      "__context__ is the foreign value")
cause = None
try:
    try:
        geometry.circleArea(-1)
    except geometry.GeometryError as inner:
        raise LookupError("explicit") from inner
except LookupError as e:
    cause = e.__cause__
check(cause is not None and cause.name == "NegativeRadius", "raise ... from <foreign value> sets __cause__")

# finally clauses run while a foreign error unwinds.
ran = []
try:
    try:
        geometry.circleArea(-1)
    finally:
        ran.append("finally")
except geometry.GeometryError:
    ran.append("handler")
check(ran == ["finally", "handler"], "finally runs, then the handler")

# In a function, through a generator and through a with statement.
def area(r):
    return geometry.circleArea(r)


def areas(radii):
    for r in radii:
        yield geometry.circleArea(r)


class Recorder:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, tb):
        self.seen = value
        return False


got = []
try:
    for a in areas([1, 2, -1, 3]):
        got.append(round(a, 3))
except geometry.NegativeRadius:
    got.append("stop")
check(got == [3.142, 12.566, "stop"], "a generator's module error reaches the loop's handler")
rec = Recorder()
try:
    with rec:
        area(-1)
except geometry.GeometryError:
    pass
check(getattr(rec, "seen", None) is not None and rec.seen.name == "NegativeRadius",
      "__exit__ receives the foreign value")

# CPython: an except clause naming a Python class that does not derive from
# BaseException is a TypeError when it is evaluated.
caught = None
try:
    try:
        geometry.circleArea(-1)
    except int:
        pass
except TypeError as e:
    caught = e
check(caught is not None and "BaseException" in str(caught), "except int raises TypeError")

# The module keeps working, and its normal results are Python floats.
check(abs(geometry.circleArea(2) - 12.566370614359172) < 1e-9, "circleArea(2)")
check(geometry.triangleArea(3, 4, 5) == 6.0, "triangleArea(3, 4, 5)")

if failures:
    print("umd_geometry_errors: FAILED (" + str(len(failures)) + ")")
else:
    print("umd_geometry_errors: PASS")
