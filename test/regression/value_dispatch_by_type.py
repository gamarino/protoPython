"""Attribute writes, descriptors and buffer arguments are decided by the
value's type, whatever its protoCore representation.

protoPython used to ask "is this value a plain protoCore object cell?" before
an attribute write, a descriptor probe or a buffer conversion.  protoCore
resolves attributes on every value through its prototype, and
getFirstParent, hasOwnAttribute, getOwnAttributeDirect and isInstanceOf answer
every value, so the runtime now asks the type question directly.  This file
pins the behaviour those sites decide, for SmallIntegers, large integers,
floats, strings, booleans, None, tuples, bytes, native methods and instances.

Mutations that must turn this file red:
- in typeChainInterceptsStore (src/library/ExecutionEngine.cpp), treat every
  class attribute that is not a function as a data descriptor: the plain
  instance write over a float or tuple class attribute then goes through
  env->setAttribute's descriptor path and `C.x` is replaced;
- in builtinAttributeWriteRejected (src/library/PythonEnvironment.cpp), return
  false early for a value that has no protoCore parent: setattr on 5, 2.5 or
  "s" then succeeds silently instead of raising AttributeError;
- in bytearray's extend (src/library/BytearrayType.cpp), skip the __iter__
  probe for a value without a parent: extend(1.5) then reports
  "'float' object is not iterable" instead of CPython's message.
"""

failures = []


def check(label, got, want):
    if got != want:
        failures.append(f"{label}: got {got!r}, want {want!r}")


def raised(fn):
    """The exception type fn() raises, or None."""
    try:
        fn()
    except Exception as exc:  # noqa: BLE001 - the type is the result
        return type(exc)
    return None


def message(fn):
    try:
        fn()
    except Exception as exc:  # noqa: BLE001
        return f"{type(exc).__name__}: {exc}"
    return None


# --- setattr / delattr on values without an instance namespace -------------
BIG = 10 ** 30
for value in (5, BIG, 2.5, "s", True, None, (1, 2), b"xy", 3j):
    check(f"setattr({type(value).__name__})", raised(lambda: setattr(value, "attr", 1)), AttributeError)
    check(f"delattr({type(value).__name__})", raised(lambda: delattr(value, "attr")), AttributeError)


def store_one(p):
    p.a = 1


def store_group(p):
    # Constant values: the compiler emits an attribute write group.
    p.a = 1
    p.b = 2
    p.c = 3


for value in (5, BIG, 2.5, "s", False, None, (1,)):
    check(f"store_one({type(value).__name__})", raised(lambda: store_one(value)), AttributeError)
    check(f"store_group({type(value).__name__})", raised(lambda: store_group(value)), AttributeError)


class Plain:
    pass


p = Plain()
store_group(p)
check("group on instance", (p.a, p.b, p.c), (1, 2, 3))

# --- class attributes of every representation are not descriptors ---------


class Holder:
    f = 2.5
    big = BIG
    t = (1, 2)
    s = "text"
    n = None
    native = len
    m = str.upper
    lst = [1]

    class Inner:
        def __set__(self, obj, value):
            raise AssertionError("a class is not a descriptor")


h = Holder()
for name in ("f", "big", "t", "s", "n", "native", "m", "lst", "Inner"):
    setattr(h, name, name + "!")
    check(f"instance write over class attr {name}", getattr(h, name), name + "!")
    check(f"class attr {name} unchanged", getattr(Holder, name) == name + "!", False)


def fast_store(obj):
    obj.f = 7
    obj.t = 8
    return obj.f + obj.t


check("STORE_ATTR over float/tuple class attrs", fast_store(Holder()), 15)

# --- data descriptors still intercept -----------------------------------------
log = []


class Descr:
    def __get__(self, obj, owner=None):
        return "got" if obj is not None else self

    def __set__(self, obj, value):
        log.append(value)


class WithDescr:
    d = Descr()


w = WithDescr()
w.d = 1
store_value = WithDescr()


def write_d(o):
    o.d = 2
    o.d = 3


write_d(store_value)
check("data descriptor __set__ runs", log, [1, 2, 3])
check("data descriptor __get__ answers", w.d, "got")


class Sub(WithDescr):
    pass


Sub().d = 4
check("inherited data descriptor", log[-1], 4)


class Prop:
    @property
    def x(self):
        return 10


check("read-only property refuses", raised(lambda: setattr(Prop(), "x", 1)), AttributeError)

# A descriptor stored on the class after instances own the name.
inst = Plain()
inst.late = "own"
Plain.late = Descr()
check("late data descriptor shadows own attr", inst.late, "got")
del Plain.late

# --- instance attributes of every representation read back as they are ---
o = Plain()
o.f, o.t, o.n, o.m, o.big, o.fn = 1.5, (1,), None, len, BIG, store_one
check("own float", o.f, 1.5)
check("own tuple", o.t, (1,))
check("own None", o.n, None)
check("own native", o.m("ab"), 2)
check("own big", o.big, BIG)
check("own function is not bound", o.fn is store_one, True)

# --- __class__ assignment -----------------------------------------------------
check("(5).__class__ = float", raised(lambda: setattr(5, "__class__", float)), TypeError)


class A:
    pass


class B:
    pass


a = A()
a.__class__ = B
check("instance __class__ assignment", type(a), B)

# --- buffer arguments are decided by type -------------------------------------
check("bytearray.extend(float)", message(lambda: bytearray().extend(1.5)),
      "TypeError: can't extend bytearray with float")
check("bytearray.extend(int)", message(lambda: bytearray().extend(5)),
      "TypeError: can't extend bytearray with int")
check("bytearray(float)", message(lambda: bytearray(1.5)),
      "TypeError: cannot convert 'float' object to bytearray")
check("bytearray(tuple)", bytearray((1, 2)), bytearray(b"\x01\x02"))
check("bytearray(list)", bytearray([1, 2]), bytearray(b"\x01\x02"))
check("bytearray(set)", bytearray({1}), bytearray(b"\x01"))
check("bytearray(range)", bytearray(range(3)), bytearray(b"\x00\x01\x02"))


class Idx:
    def __index__(self):
        return 3


check("bytearray(__index__)", bytearray(Idx()), bytearray(3))
check("bytearray * __index__", bytearray(b"a") * Idx(), bytearray(b"aaa"))
check("bytearray * float", raised(lambda: bytearray(b"a") * 1.5), TypeError)
check("bytearray[float]", raised(lambda: bytearray(b"ab")[1.5]), TypeError)
check("bytearray[slice]", bytearray(b"ab")[0:1], bytearray(b"a"))
check("bytearray.partition", bytearray(b"a,b").partition(b","),
      (bytearray(b"a"), bytearray(b","), bytearray(b"b")))
check("memoryview(int)", raised(lambda: memoryview(5)), TypeError)
check("memoryview(float)", raised(lambda: memoryview(1.5)), TypeError)
check("memoryview(tuple)", raised(lambda: memoryview((1,))), TypeError)
check("memoryview slice", memoryview(b"ab")[0:1].tobytes(), b"a")
check("bytes.hex(bytes sep)", b"\x01\x02".hex(b":"), "01:02")
check("bytes.hex(float sep)", raised(lambda: b"\x01\x02".hex(1.5)), TypeError)
check("bytes.center(bytes fill)", b"a".center(3, b"*"), b"*a*")
check("bytes.center(bytearray fill)", b"a".center(3, bytearray(b"*")), b"*a*")
check("bytes.center(float fill)", raised(lambda: b"a".center(3, 1.5)), TypeError)

import codecs  # noqa: E402

check("codecs.decode(bytes)", codecs.decode(b"abc", "ascii"), "abc")
check("codecs.decode(bytearray)", codecs.decode(bytearray(b"abc"), "ascii"), "abc")

# --- collections --------------------------------------------------------------
lst = [1, 2, 3, 4]
lst[1:3] = (9, 8, 7)
check("list slice from tuple", lst, [1, 9, 8, 7, 4])
lst[0:1] = [5]
check("list slice from list", lst, [5, 9, 8, 7, 4])
lst[0:2] = iter((0,))
check("list slice from iterator", lst, [0, 8, 7, 4])


class MyList(list):
    pass


lst[0:1] = MyList([6, 6])
check("list slice from list subclass", lst, [6, 6, 8, 7, 4])

check("dict from pairs", dict([(1, 2), [3, 4], MyList([5, 6])]), {1: 2, 3: 4, 5: 6})
check("iter dict", list(iter({1: 2, 3: 4})), [1, 3])
check("set ops", ({1, 2} | frozenset({3})) - {1}, {2, 3})
check("set with dict", {1, 2} - {1: 0}.keys(), {2})
check("dict view ops", {1: 0, 2: 0}.keys() & {2, 3}, {2})


class S(str):
    pass


g = {"name": 1}
check("module namespace with str subclass key", S("name") in g, True)
import sys  # noqa: E402

mod = sys.modules[__name__]
check("module attribute lookup via str subclass", getattr(mod, S("check")) is check, True)

d = {}
exec("x = 1", {}, d)
check("exec with dict locals", d, {"x": 1})

if failures:
    for f in failures:
        print("FAIL", f)
    raise SystemExit(1)
print("value_dispatch_by_type: ok")
