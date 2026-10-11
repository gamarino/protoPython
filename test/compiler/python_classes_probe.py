# Compiled by protopyc in test/compiler/protopyc_python_classes.sh; the driver
# (python_classes_driver.py) must print what CPython prints for this source.
import enum
from dataclasses import dataclass, field

LOG = []


# -- attribute reads ---------------------------------------------------------

class Plain:
    kind = "plain"

    def __init__(self, x):
        self.x = x

    @property
    def double(self):
        return self.x * 2

    @classmethod
    def make(cls, x):
        return cls(x)

    @staticmethod
    def helper(a):
        return a + 1

    def method(self):
        return "method of " + str(self.x)


class Fallback:
    def __init__(self):
        self.real = 1

    def __getattr__(self, name):
        return "fallback:" + name


class Intercept:
    def __getattribute__(self, name):
        if name == "secret":
            raise KeyError(name)
        return "intercepted:" + name


class Ten:
    def __get__(self, obj, owner):
        return 10 if obj is None else obj.base * 10


class Logged:
    def __init__(self):
        self.value = 0

    def __get__(self, obj, owner):
        if obj is None:
            return self
        return obj.__dict__.get("_logged", 0)

    def __set__(self, obj, value):
        LOG.append(("set", value))
        obj.__dict__["_logged"] = value


class Guarded:
    def __init__(self):
        self.seen = []

    def __setattr__(self, name, value):
        if name != "seen":
            self.seen.append(name)
        object.__setattr__(self, name, value.upper() if isinstance(value, str) else value)


class WithDescriptors:
    ten = Ten()
    logged = Logged()

    def __init__(self, base):
        self.base = base


def attribute_reads():
    out = []
    p = Plain(4)
    out.append(p.x)
    out.append(p.kind)
    out.append(p.double)
    out.append(Plain.make(5).x)
    out.append(p.make(6).x)
    out.append(p.helper(1))
    out.append(Plain.helper(2))
    bound = p.method
    out.append(bound())
    try:
        p.missing
        out.append("no error")
    except AttributeError as e:
        out.append(("AttributeError", str(e)))
    try:
        Plain.missing
        out.append("no error")
    except AttributeError as e:
        out.append(("AttributeError", str(e)))
    f = Fallback()
    out.append(f.real)
    out.append(f.other)
    i = Intercept()
    out.append(i.anything)
    try:
        i.secret
    except KeyError as e:
        out.append(("KeyError", str(e)))
    w = WithDescriptors(3)
    out.append(w.ten)
    out.append(WithDescriptors.ten)
    w.logged = 7
    out.append(w.logged)
    out.append(LOG[:])
    g = Guarded()
    g.label = "low"
    out.append((g.label, g.seen))
    out.append((1.5).real)
    append = out.append
    append("via bound builtin")
    try:
        None.nothing
    except AttributeError as e:
        out.append(("AttributeError", str(e)))
    return out


# -- classes -----------------------------------------------------------------

class Base:
    greeting = "hello"

    def __init__(self, name):
        self.name = name

    def greet(self):
        return self.greeting + " " + self.name

    def describe(self):
        return "Base(" + self.name + ")"

    def __repr__(self):
        return "<" + type(self).__name__ + " " + self.name + ">"

    def __eq__(self, other):
        return isinstance(other, Base) and self.name == other.name

    def __hash__(self):
        return hash(self.name)

    def __len__(self):
        return len(self.name)

    def __iter__(self):
        return iter(self.name)


class Derived(Base):
    greeting = "hi"

    def describe(self):
        return "Derived/" + super().describe()


class Managed:
    def __init__(self):
        self.events = []

    def __enter__(self):
        self.events.append("enter")
        return self

    def __exit__(self, t, v, tb):
        self.events.append(("exit", None if t is None else t.__name__))
        return t is not None and issubclass(t, KeyError)


class ProbeError(Exception):
    def __init__(self, code):
        super().__init__("code " + str(code))
        self.code = code


SUBCLASSES = []


class Registry:
    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        SUBCLASSES.append(cls.__name__)


class Registered(Registry):
    pass


class Counter:
    count = 0

    def __init__(self):
        Counter.count += 1


def class_facts():
    out = []
    out.append([c.__name__ for c in Derived.__mro__])
    out.append([c.__name__ for c in Derived.__bases__])
    out.append(Derived.__name__)
    out.append(Derived.__qualname__)
    out.append(Derived.__module__)
    d = Derived("ann")
    out.append(type(d) is Derived)
    out.append(type(d).__name__)
    out.append(isinstance(d, Base))
    out.append(issubclass(Derived, Base))
    out.append(issubclass(Base, Derived))
    out.append(d.greet())
    out.append(d.describe())
    out.append(repr(d))
    out.append(d == Derived("ann"))
    out.append(len(d))
    out.append(list(d))
    out.append(type(Derived).__name__)
    try:
        raise ProbeError(7)
    except ProbeError as e:
        out.append((type(e).__name__, e.code, str(e), isinstance(e, Exception)))
    out.append(SUBCLASSES[:])
    Counter()
    Counter()
    out.append(Counter.count)
    with Managed() as m:
        m.events.append("body")
    out.append(m.events)
    return out


def make_local_class(tag):
    class Local(Base):
        def describe(self):
            return tag + ":" + Base.describe(self)
    return Local


# -- closures in methods -----------------------------------------------------

def lambda_method_class(k):
    class Holder:
        get = lambda self: k
        add = lambda self, n: k + n
    return Holder


def attached_method_class(k):
    def outside(self):
        return ("outside", k)
    late = lambda self: ("late", k)

    class Target:
        pass
    Target.outside = outside
    Target.late = late
    return Target


def closures():
    out = []
    h = lambda_method_class(5)()
    out.append(h.get())
    out.append(h.add(2))
    t = attached_method_class("k")()
    out.append(t.outside())
    out.append(t.late())
    L = make_local_class("tagged")
    out.append(L("bob").describe())
    return out


# -- class machinery ---------------------------------------------------------

class Meta(type):
    def __new__(mcls, name, bases, ns):
        ns["made_by"] = "Meta"
        return super().__new__(mcls, name, bases, ns)


class WithMeta(metaclass=Meta):
    pass


class Color(enum.Enum):
    RED = 1
    GREEN = 2


@dataclass
class Point:
    x: int
    y: int = 0
    tags: list = field(default_factory=list)

    def norm1(self):
        return abs(self.x) + abs(self.y)


class Slotted:
    __slots__ = ("a",)

    def __init__(self):
        self.a = 1


class Outer:
    class Inner:
        pass


def class_machinery():
    out = []
    out.append((type(WithMeta).__name__, WithMeta.made_by))
    out.append((Color.RED.name, Color.GREEN.value, [c.name for c in Color], Color(2) is Color.GREEN))
    p = Point(3, -4)
    out.append((repr(Point(1)), p == Point(3, -4), p.norm1()))
    out.append(Point.__annotations__ == {"x": int, "y": int, "tags": list})
    s = Slotted()
    try:
        s.b = 2
        out.append("no error")
    except AttributeError:
        out.append(("AttributeError", s.a))
    out.append((Outer.Inner.__qualname__, make_local_class("q").__qualname__))
    out.append(LOG[:])
    return out


# -- exception chaining ------------------------------------------------------

def chaining():
    import sys
    out = []
    try:
        try:
            raise KeyError("a")
        except KeyError:
            raise ValueError("b")
    except ValueError as e:
        out.append(("in except", type(e.__context__).__name__))
    try:
        try:
            raise KeyError("a")
        finally:
            1 / 0
    except ZeroDivisionError as e:
        out.append(("in finally", type(e.__context__).__name__))
    try:
        try:
            raise KeyError("a")
        except KeyError as k:
            raise ValueError("c") from k
    except ValueError as e:
        out.append(("from", type(e.__cause__).__name__, e.__suppress_context__))
    try:
        try:
            raise KeyError("x")
        finally:
            out.append(("exc_info in finally", type(sys.exc_info()[1]).__name__))
    except KeyError:
        pass
    out.append(("exc_info after", sys.exc_info()[1]))
    return out
