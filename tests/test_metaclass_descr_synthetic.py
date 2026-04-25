"""
Synthetic metaclass + descriptor baseline (PG-round prep).

Mirrors the structure of test_generators_synthetic.py: small focused
cases driven by `_run` so the suite always runs to completion and
records each test's outcome (PASS / FAIL / CRASH).  Used as the
objective metric for the metaclass + descriptor work.
"""

import sys

_results = []

def _run(fn):
    try:
        fn()
        _results.append(("PASS", fn.__name__, ""))
    except AssertionError as e:
        _results.append(("FAIL", fn.__name__, str(e) or "assertion"))
    except BaseException as e:
        _results.append(("CRASH", fn.__name__, f"{type(e).__name__}: {e}"))
    return fn


# --- Class basics: type identity, MRO, __mro__ ---------------------------

@_run
def test_type_of_simple_class():
    class C: pass
    assert type(C) is type, type(C)


@_run
def test_type_of_instance():
    class C: pass
    c = C()
    assert type(c) is C, type(c)


@_run
def test_isinstance_basic():
    class C: pass
    c = C()
    assert isinstance(c, C)
    assert not isinstance(c, int)


@_run
def test_issubclass_basic():
    class A: pass
    class B(A): pass
    assert issubclass(B, A)
    assert issubclass(A, A)
    assert not issubclass(A, B)


@_run
def test_mro_diamond():
    class A: pass
    class B(A): pass
    class C(A): pass
    class D(B, C): pass
    mro = D.__mro__
    assert mro[0] is D, mro
    assert mro[1] is B, mro
    assert mro[2] is C, mro
    assert mro[3] is A, mro


# --- Metaclass: explicit metaclass= ----------------------------------------

@_run
def test_metaclass_basic():
    class Meta(type): pass
    class C(metaclass=Meta): pass
    assert type(C) is Meta, type(C)
    assert isinstance(C, Meta)
    assert isinstance(C, type)


@_run
def test_metaclass_inherited():
    class Meta(type): pass
    class C(metaclass=Meta): pass
    class D(C): pass
    assert type(D) is Meta, type(D)


@_run
def test_metaclass_new_called():
    seen = []
    class Meta(type):
        def __new__(mcs, name, bases, namespace):
            seen.append(name)
            return super().__new__(mcs, name, bases, namespace)
    class C(metaclass=Meta): pass
    assert seen == ["C"], seen


@_run
def test_metaclass_init_called():
    seen = []
    class Meta(type):
        def __init__(cls, name, bases, namespace):
            seen.append(("init", name))
            super().__init__(name, bases, namespace)
    class C(metaclass=Meta): pass
    assert ("init", "C") in seen, seen


@_run
def test_metaclass_call_creates_instance():
    class Meta(type): pass
    class C(metaclass=Meta):
        def __init__(self, x):
            self.x = x
    c = C(42)
    assert c.x == 42


# --- __init_subclass__ -----------------------------------------------------

@_run
def test_init_subclass_called_on_subclass():
    seen = []
    class Base:
        def __init_subclass__(cls, **kwargs):
            seen.append(cls.__name__)
            super().__init_subclass__(**kwargs)
    class Sub(Base): pass
    assert seen == ["Sub"], seen


@_run
def test_init_subclass_kwargs():
    captured = {}
    class Base:
        def __init_subclass__(cls, *, label=None, **kwargs):
            captured["label"] = label
            super().__init_subclass__(**kwargs)
    class Sub(Base, label="hello"): pass
    assert captured["label"] == "hello", captured


# --- __set_name__ ----------------------------------------------------------

@_run
def test_set_name_called_on_descriptor():
    seen = []
    class Tracker:
        def __set_name__(self, owner, name):
            seen.append((owner.__name__, name))
    class C:
        attr = Tracker()
    assert seen == [("C", "attr")], seen


# --- Data descriptors: __get__, __set__, __delete__ -----------------------

@_run
def test_data_descriptor_get():
    class D:
        def __get__(self, obj, objtype=None):
            return 42
        def __set__(self, obj, value):
            obj.__dict__["x"] = value
    class C:
        x = D()
    c = C()
    assert c.x == 42


@_run
def test_data_descriptor_overrides_instance_dict():
    class D:
        def __get__(self, obj, objtype=None):
            return "from_descriptor"
        def __set__(self, obj, value):
            obj.__dict__["x"] = value
    class C:
        x = D()
    c = C()
    c.__dict__["x"] = "from_instance"
    assert c.x == "from_descriptor", c.x


@_run
def test_data_descriptor_set():
    captured = []
    class D:
        def __get__(self, obj, objtype=None):
            return obj.__dict__.get("x", 0)
        def __set__(self, obj, value):
            captured.append(value)
            obj.__dict__["x"] = value
    class C:
        x = D()
    c = C()
    c.x = 99
    assert captured == [99], captured
    assert c.x == 99, c.x


@_run
def test_data_descriptor_delete():
    deleted = []
    class D:
        def __get__(self, obj, objtype=None):
            return obj.__dict__.get("x")
        def __set__(self, obj, value):
            obj.__dict__["x"] = value
        def __delete__(self, obj):
            deleted.append(True)
            obj.__dict__.pop("x", None)
    class C:
        x = D()
    c = C()
    c.x = 5
    del c.x
    assert deleted == [True], deleted


# --- Non-data descriptors (only __get__) ----------------------------------

@_run
def test_non_data_descriptor_overridden_by_instance():
    class D:
        def __get__(self, obj, objtype=None):
            return "from_descriptor"
    class C:
        x = D()
    c = C()
    c.__dict__["x"] = "from_instance"
    assert c.x == "from_instance", c.x


# --- property ---------------------------------------------------------------

@_run
def test_property_get():
    class C:
        @property
        def x(self):
            return 7
    c = C()
    assert c.x == 7


@_run
def test_property_set_via_setter():
    class C:
        def __init__(self):
            self._x = 0
        @property
        def x(self):
            return self._x
        @x.setter
        def x(self, v):
            self._x = v * 2
    c = C()
    c.x = 5
    assert c.x == 10, c.x


@_run
def test_property_readonly_raises():
    class C:
        @property
        def x(self):
            return 1
    c = C()
    try:
        c.x = 99
    except AttributeError:
        return
    raise AssertionError("expected AttributeError on read-only property")


# --- classmethod / staticmethod -------------------------------------------

@_run
def test_classmethod_binds_class():
    class C:
        @classmethod
        def m(cls):
            return cls
    assert C.m() is C
    assert C().m() is C


@_run
def test_staticmethod_no_self():
    class C:
        @staticmethod
        def m(x):
            return x + 1
    assert C.m(5) == 6
    assert C().m(5) == 6


# --- __getattr__ vs __getattribute__ --------------------------------------

@_run
def test_getattr_fallback():
    class C:
        def __getattr__(self, name):
            return f"missing:{name}"
    c = C()
    c.real = 1
    assert c.real == 1
    assert c.absent == "missing:absent", c.absent


@_run
def test_getattribute_overrides_all():
    class C:
        def __getattribute__(self, name):
            if name == "force":
                return "got_force"
            return super().__getattribute__(name)
    c = C()
    c.x = 5
    assert c.x == 5
    assert c.force == "got_force"


# --- class body locals: __qualname__, __name__ ---------------------------

@_run
def test_class_qualname():
    class C: pass
    assert C.__name__ == "C", C.__name__
    assert C.__qualname__ == "C", C.__qualname__


@_run
def test_nested_class_qualname():
    class Outer:
        class Inner: pass
    assert Outer.Inner.__qualname__.endswith("Outer.Inner"), Outer.Inner.__qualname__


# --- __slots__ -------------------------------------------------------------

@_run
def test_slots_basic():
    class C:
        __slots__ = ("x", "y")
    c = C()
    c.x = 1
    c.y = 2
    assert c.x == 1
    assert c.y == 2


@_run
def test_slots_rejects_extra_attr():
    class C:
        __slots__ = ("x",)
    c = C()
    c.x = 1
    try:
        c.z = 99
    except AttributeError:
        return
    raise AssertionError("expected AttributeError when assigning unslotted attr")


# --- ABC: abstractmethod via abc ---------------------------------------

@_run
def test_abstract_class_cannot_instantiate():
    from abc import ABC, abstractmethod
    class A(ABC):
        @abstractmethod
        def m(self): ...
    try:
        A()
    except TypeError:
        return
    raise AssertionError("expected TypeError instantiating abstract class")


# --- super() in methods ---------------------------------------------------

@_run
def test_super_basic_call():
    class A:
        def m(self):
            return "A"
    class B(A):
        def m(self):
            return "B+" + super().m()
    assert B().m() == "B+A", B().m()


@_run
def test_super_three_levels():
    class A:
        def m(self):
            return "A"
    class B(A):
        def m(self):
            return "B+" + super().m()
    class C(B):
        def m(self):
            return "C+" + super().m()
    assert C().m() == "C+B+A", C().m()


# --- __new__ override ----------------------------------------------------

@_run
def test_new_override_singleton():
    class C:
        _instance = None
        def __new__(cls):
            if cls._instance is None:
                cls._instance = object.__new__(cls)
            return cls._instance
    a = C()
    b = C()
    assert a is b


# --- type() three-arg form (dynamic class creation) ----------------------

@_run
def test_type_three_arg():
    C = type("Dyn", (), {"x": 99})
    assert C.__name__ == "Dyn", C.__name__
    assert C.x == 99
    assert isinstance(C(), C)


# --- ChainMap / dict-like attribute lookup --------------------------------

@_run
def test_class_attribute_lookup_walks_mro():
    class A:
        x = 1
    class B(A): pass
    class C(B): pass
    assert C.x == 1
    c = C()
    assert c.x == 1


# --- vars(), __dict__ ----------------------------------------------------

@_run
def test_instance_dict_visible_via_vars():
    class C:
        pass
    c = C()
    c.a = 10
    c.b = 20
    d = vars(c)
    assert d.get("a") == 10, d
    assert d.get("b") == 20, d


# --- Bound vs unbound methods --------------------------------------------

@_run
def test_unbound_method_via_class():
    class C:
        def m(self):
            return self
    c = C()
    # Accessing via class returns the function; via instance returns bound.
    func = C.m
    bound = c.m
    assert func(c) is c
    assert bound() is c


# --- Reporting ------------------------------------------------------------

print("=" * 60)
for status, name, msg in _results:
    if status == "PASS":
        print(f"PASS  {name}")
    else:
        print(f"{status} {name}  ({msg})")
n_pass = sum(1 for r in _results if r[0] == "PASS")
n_fail = sum(1 for r in _results if r[0] == "FAIL")
n_crash = sum(1 for r in _results if r[0] == "CRASH")
print("=" * 60)
print(f"PASS={n_pass}  FAIL={n_fail}  CRASH={n_crash}  TOTAL={len(_results)}")
