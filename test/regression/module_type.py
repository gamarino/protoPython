# Regression: the module type is <class 'module'>.
# modulePrototype's own `__name__` is the getset descriptor module
# instances read, so the class itself reported that descriptor as its
# name: type(sys) and types.ModuleType printed as <class 'object'>, and
# ModuleType was not a class (no __bases__).
import sys
import types
import inspect

MT = types.ModuleType
assert type(sys) is MT
assert type(types) is MT
assert MT.__name__ == 'module', MT.__name__
assert MT.__qualname__ == 'module', MT.__qualname__
assert MT.__module__ == 'builtins'
assert repr(MT) == "<class 'module'>", repr(MT)
assert str(type(sys)) == "<class 'module'>", str(type(sys))
assert getattr(MT, '__name__') == 'module'
assert MT.__bases__ == (object,), MT.__bases__
assert MT.__mro__ == (MT, object)
assert isinstance(MT, type)
assert inspect.isclass(MT)
assert isinstance(sys, MT)

# Module instances keep their own names.
assert sys.__name__ == 'sys'
# The type's class attributes are not module globals or namespace entries.
import json
for attr in ('__bases__', '__mro__', '__qualname__', '__module__', '__is_python_class__'):
    assert attr not in vars(json), attr
    try:
        eval(attr)
    except NameError:
        pass
    else:
        raise AssertionError(attr + " resolved as a module global")
m = MT('m')
assert m.__name__ == 'm'
assert isinstance(m, MT)
m2 = MT('m2', 'doc')
assert m2.__doc__ == 'doc'
# An uninitialised module still has no __name__ (STRUCT-90).
assert not hasattr(MT.__new__(MT), '__name__')


class MyModule(MT):
    def __init__(self, name):
        MT.__init__(self, name)
        self.extra = 1


mm = MyModule('mine')
assert mm.__name__ == 'mine'
assert mm.extra == 1
assert type(mm) is MyModule
assert isinstance(mm, MT)
assert MyModule.__name__ == 'MyModule'
assert issubclass(MyModule, MT)


# The same conflict for a class whose `__name__` is a slot.
class Named:
    __slots__ = ('__name__',)


assert Named.__name__ == 'Named', Named.__name__
assert repr(Named).endswith("Named'>"), repr(Named)
n = Named()
n.__name__ = 'inst'
assert n.__name__ == 'inst'

print("module_type OK")
