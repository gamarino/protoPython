# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.

"""Mixin and abstract methods of the collections.abc ABCs.

protoPython's ``_collections_abc`` is a native module: it creates the ABC
class objects with CPython's hierarchy, keeps their registries of virtual
subclasses, answers isinstance/issubclass through the subclass hooks, and
computes __abstractmethods__ (of the ABCs and of every class derived from
one), the attributes the class bodies set to None and the names.  The
methods the ABCs define are CPython's own (Lib/_collections_abc.py) and live
in the modules of this package, one per family of ABCs, unchanged wherever
possible.  Each method starts on its ABC as a native trampoline; the first
call of one imports its family's module and calls its ``install(module)``,
which puts that family's functions in their place.  A family needs no other
family loaded: the methods reach other ABCs' methods through the classes.
These modules must import nothing but ``sys`` and this package (never
``_collections_abc`` or ``collections``).

The methods are module-level functions attached to the classes, not class
bodies: the bodies would need ABCMeta and the native classes as bases (and
a class body's namespace would shadow the names it defines).
Differences from Lib/_collections_abc.py:

* The ABC names used inside the methods (``Set``, ``Mapping``, ...) are the
  native classes; install() binds them as globals of the family's module.
* MutableMapping's private ``__marker`` default is the module's ``_marker``.
* ``@abstractmethod`` is applied by hand (``__isabstractmethod__``).
"""

# The class methods of the ABCs.
_CLASS_METHODS = ("_from_iterable",)


def define(table, abc_name, name, abstract=False):
    """A decorator recording the function in `table` as `abc_name.name`."""
    def register(func):
        func.__name__ = name
        func.__qualname__ = abc_name + "." + name
        func.__module__ = "collections.abc"
        if abstract:
            func.__isabstractmethod__ = True
        table.setdefault(abc_name, {})[name] = func
        return func
    return register


def alias(table, abc_name, name, other):
    """`abc_name.name = abc_name.other`, as `__rand__ = __and__` does."""
    table[abc_name][name] = table[abc_name][other]


def install(module, table, namespace, names):
    """Bind the ABCs of `table` and `names` in `namespace` (the functions'
    globals) and put the functions of `table` on their ABCs of `module`.
    Running it twice assigns the same functions again."""
    for abc_name in table:
        namespace[abc_name] = getattr(module, abc_name)
    for abc_name in names:
        namespace[abc_name] = getattr(module, abc_name)
    for abc_name, methods in table.items():
        cls = namespace[abc_name]
        for attr, func in methods.items():
            setattr(cls, attr, classmethod(func) if attr in _CLASS_METHODS else func)
