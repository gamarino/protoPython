# Built-in types are immutable and their instances without __dict__ accept no
# new attribute, as in CPython (review round 6: the documented deviation
# "built-in types and instances accept new attributes" is gone).
import types

def raises(exc, fn, text=None):
    try:
        fn()
    except exc as e:
        if text is not None:
            assert text in str(e), (str(e), text)
        return
    raise AssertionError("%s not raised" % exc.__name__)

# Types: TypeError, through every route.
raises(TypeError, lambda: setattr(list, 'x', 1), "cannot set 'x' attribute of immutable type 'list'")
raises(TypeError, lambda: setattr(int, 'x', 1), "immutable type 'int'")
raises(TypeError, lambda: setattr(str, 'upper', 1), "cannot set 'upper' attribute of immutable type 'str'")
raises(TypeError, lambda: setattr(ValueError, 'x', 1), "immutable type 'ValueError'")
raises(TypeError, lambda: setattr(type(None), 'x', 1), "immutable type 'NoneType'")
raises(TypeError, lambda: setattr(types.FunctionType, 'x', 1), "immutable type 'function'")
raises(TypeError, lambda: setattr(types.ModuleType, 'x', 1), "immutable type 'module'")
raises(TypeError, lambda: type.__setattr__(list, 'x', 1), "immutable type 'list'")
raises(TypeError, lambda: object.__setattr__(dict, 'x', 1))
raises(TypeError, lambda: delattr(list, 'append'), "cannot set 'append' attribute of immutable type 'list'")
def store_on_type():
    bytearray.x = 1
raises(TypeError, store_on_type, "immutable type 'bytearray'")
def override_len():
    list.__len__ = lambda s: 42
raises(TypeError, override_len)
assert len([1, 2, 3]) == 3

# Instances without __dict__: AttributeError.
raises(AttributeError, lambda: setattr({}, 'x', 1),
       "'dict' object has no attribute 'x' and no __dict__ for setting new attributes")
raises(AttributeError, lambda: setattr((1,), 'x', 1), "'tuple' object has no attribute 'x'")
raises(AttributeError, lambda: setattr(bytearray(), 'x', 1), "'bytearray' object has no attribute 'x'")
raises(AttributeError, lambda: setattr(len, 'x', 1), "'builtin_function_or_method' object has no attribute 'x'")
raises(AttributeError, lambda: setattr([], 'append', 1), "'list' object attribute 'append' is read-only")
raises(AttributeError, lambda: delattr([], 'append'), "'list' object attribute 'append' is read-only")
raises(AttributeError, lambda: setattr(object(), 'x', 1))
raises(AttributeError, lambda: setattr(None, 'x', 1))
raises(AttributeError, lambda: setattr(range(3), 'x', 1))
def store_on_instance():
    l = [1]
    l.__len__ = lambda: 99
raises(AttributeError, store_on_instance)
def store_attr_opcode():
    d = {}
    d.x = 1
raises(AttributeError, store_attr_opcode)

# What keeps working: classes, their instances, subclasses of built-ins,
# functions, modules, exception instances.
class C:
    pass
C.x = 1
c = C()
c.y = 2
del C.x
class L(list):
    pass
l = L()
l.tag = 1
assert l.tag == 1
L.extra = 2
def fn():
    pass
fn.attr = 3
assert fn.attr == 3
m = types.ModuleType('m')
m.v = 4
del m.v
e = ValueError()
e.note = 5
assert e.note == 5
class B(bytes):
    pass
b = B(b'x')
assert type(b) is B
b.tag = 6
assert b.tag == 6 and b == b'x'

# dir() lists __dict__ / __weakref__ only where CPython does.
for t in (bytearray, bytes, list, int, tuple, len, [], 1):
    assert '__dict__' not in dir(t) and '__weakref__' not in dir(t), t
assert '__dict__' in dir(C) and '__weakref__' in dir(C)
assert '__dict__' in dir(c) and '__dict__' in dir(l)
assert '__dict__' in dir(ValueError) and '__dict__' in dir(fn)
print("builtin_type_attributes: ok")
