# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.

"""Abstract Base Classes (ABCs) for collections, according to PEP 3119.

Unit tests are in test_collections.
"""

############ Maintenance notes #########################################
#
# ABCs are different from other standard library modules in that they
# specify compliance tests.  In general, once an ABC has been published,
# new methods (either abstract or concrete) cannot be added.
#
# Though classes that inherit from an ABC would automatically receive a
# new mixin method, registered classes would become non-compliant and
# violate the contract promised by ``isinstance(someobj, SomeABC)``.
#
# Though irritating, the correct procedure for adding new abstract or
# mixin methods is to create a new ABC as a subclass of the previous
# ABC.  For example, union(), intersection(), and difference() cannot
# be added to Set but could go into a new ABC that extends Set.
#
# Because they are so hard to change, new ABCs should have their APIs
# carefully thought through prior to publication.
#
# Since ABCMeta only checks for the presence of methods, it is possible
# to alter the signature of a method by adding optional arguments
# or changing parameters names.  This is still a bit dubious but at
# least it won't cause isinstance() to return an incorrect result.
#
#
#######################################################################

from abc import ABCMeta, abstractmethod
import sys

GenericAlias = type(list[int])
EllipsisType = type(...)
def _f(): pass
FunctionType = type(_f)
del _f

__all__ = ["Awaitable", "Coroutine",
           "AsyncIterable", "AsyncIterator", "AsyncGenerator",
           "Hashable", "Iterable", "Iterator", "Generator", "Reversible",
           "Sized", "Container", "Callable", "Collection",
           "Set", "MutableSet",
           "Mapping", "MutableMapping",
           "MappingView", "KeysView", "ItemsView", "ValuesView",
           "Sequence", "MutableSequence",
           "ByteString", "Buffer",
           ]

# This module has been renamed from collections.abc to _collections_abc to
# speed up interpreter startup. Some of the types such as MutableMapping are
# required early but collections module imports a lot of other modules.
# See issue #19218
__name__ = "collections.abc"

# Private list of types that we want to register with the various ABCs
# so that they will pass tests like:
#       it = iter(somebytearray)
#       assert isinstance(it, Iterable)
# Note:  in other implementations, these types might not be distinct
# and they may have their own implementation specific types that
# are not included on this list.
bytes_iterator = type(iter(b''))
bytearray_iterator = type(iter(bytearray()))
#callable_iterator = ???
dict_keyiterator = type(iter({}.keys()))
dict_valueiterator = type(iter({}.values()))
dict_itemiterator = type(iter({}.items()))
list_iterator = type(iter([]))
list_reverseiterator = type(iter(reversed([])))
range_iterator = type(iter(range(0)))
longrange_iterator = type(iter(range(1 << 1000)))
set_iterator = type(iter(set()))
str_iterator = type(iter(""))
tuple_iterator = type(iter(()))
zip_iterator = type(iter(zip()))
## views ##
dict_keys = type({}.keys())
dict_values = type({}.values())
dict_items = type({}.items())
## misc ##
mappingproxy = type(type.__dict__)
generator = type((lambda: (yield))())
## coroutine ##
async def _coro(): pass
_coro = _coro()
coroutine = type(_coro)
_coro.close()  # Prevent ResourceWarning
del _coro
## asynchronous generator ##
async def _ag(): yield
_ag = _ag()
async_generator = type(_ag)
del _ag


### ONE-TRICK PONIES ###

def _check_methods(C, *methods):
    mro = C.__mro__
    for method in methods:
        for B in mro:
            if method in B.__dict__:
                if B.__dict__[method] is None:
                    return NotImplemented
                break
        else:
            return NotImplemented
    return True

class Hashable(metaclass=ABCMeta):

    __slots__ = ()

    @abstractmethod
    def __hash__(self):
        return 0

    @classmethod
    def __subclasshook__(cls, C):
        if cls is Hashable:
            return _check_methods(C, "__hash__")
        return NotImplemented


class Awaitable(metaclass=ABCMeta):

    __slots__ = ()

    @abstractmethod
    def __await__(self):
        yield

    @classmethod
    def __subclasshook__(cls, C):
        if cls is Awaitable:
            return _check_methods(C, "__await__")
        return NotImplemented

    __class_getitem__ = classmethod(GenericAlias)


class Coroutine(Awaitable):

    __slots__ = ()

    @abstractmethod
    def send(self, value):
        """Send a value into the coroutine.
        Return next yielded value or raise StopIteration.
        """
        raise StopIteration

    @abstractmethod
    def throw(self, typ, val=None, tb=None):
        """Raise an exception in the coroutine.
        Return next yielded value or raise StopIteration.
        """
        if val is None:
            if tb is None:
                raise typ
            val = typ()
        if tb is not None:
            val = val.with_traceback(tb)
        raise val

    def close(self):
        """Raise GeneratorExit inside coroutine.
        """
        try:
