# Copyright 2007 Google, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# The method bodies below are those of CPython 3.14's Lib/_collections_abc.py.

"""Mixin and abstract methods of the collections.abc ABCs.

protoPython's ``_collections_abc`` is a native module: it creates the ABC
class objects with CPython's hierarchy, keeps their registries of virtual
subclasses and answers isinstance/issubclass through the subclass hooks.
The methods the ABCs define are CPython's own and live here, unchanged
wherever possible.  The native module calls ``install(module)`` once, while
it is being imported; this module must therefore import nothing but
``sys`` (never ``_collections_abc`` or ``collections``).

The methods are module-level functions attached to the classes, not class
bodies: the bodies would need ABCMeta and the native classes as bases (and
a class body's namespace would shadow the names it defines).
Differences from Lib/_collections_abc.py:

* The ABC names used inside the methods (``Set``, ``Mapping``, ...) are the
  native classes; install() binds them as globals of this module.
* MutableMapping's private ``__marker`` default is the module's ``_marker``.
* ``@abstractmethod`` is applied by hand (``__isabstractmethod__``), and
  ``__abstractmethods__`` is computed the way ABCMeta does: by install()
  for the ABCs, and for every class derived from one by an
  ``__init_subclass__`` the root ABCs carry, as they have no ABCMeta.
* A class that defines ``__eq__`` without ``__hash__`` gets ``__hash__ =
  None`` from ``type``; install() sets it explicitly on Set and Mapping.
* install() makes the registrations of built-in types the native module
  does not make (iterator and dict view types).
"""

import sys

# Filled by _define(): ABC name -> {attribute name -> value}.
_METHODS = {}


def _define(abc_name, name, abstract=False):
    """Record the decorated function as `abc_name.name`."""
    def register(func):
        func.__name__ = name
        func.__qualname__ = abc_name + "." + name
        if abstract:
            func.__isabstractmethod__ = True
        _METHODS.setdefault(abc_name, {})[name] = func
        return func
    return register


def _alias(abc_name, name, other):
    """`abc_name.name = abc_name.other`, as `__rand__ = __and__` does."""
    _METHODS[abc_name][name] = _METHODS[abc_name][other]


### ONE-TRICK PONIES ###

@_define("Hashable", "__hash__", abstract=True)
def _hashable_hash(self):
    return 0


@_define("Awaitable", "__await__", abstract=True)
def _awaitable_await(self):
    yield


@_define("Coroutine", "send", abstract=True)
def _coroutine_send(self, value):
    """Send a value into the coroutine.
    Return next yielded value or raise StopIteration.
    """
    raise StopIteration


@_define("Coroutine", "throw", abstract=True)
def _coroutine_throw(self, typ, val=None, tb=None):
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


@_define("Coroutine", "close")
def _coroutine_close(self):
    """Raise GeneratorExit inside coroutine.
    """
    try:
        self.throw(GeneratorExit)
    except (GeneratorExit, StopIteration):
        pass
    else:
        raise RuntimeError("coroutine ignored GeneratorExit")


@_define("AsyncIterable", "__aiter__", abstract=True)
def _async_iterable_aiter(self):
    return AsyncIterator()


@_define("AsyncIterator", "__anext__", abstract=True)
async def _async_iterator_anext(self):
    """Return the next item or raise StopAsyncIteration when exhausted."""
    raise StopAsyncIteration


@_define("AsyncIterator", "__aiter__")
def _async_iterator_aiter(self):
    return self


@_define("AsyncGenerator", "__anext__")
async def _async_generator_anext(self):
    """Return the next item from the asynchronous generator.
    When exhausted, raise StopAsyncIteration.
    """
    return await self.asend(None)


@_define("AsyncGenerator", "asend", abstract=True)
async def _async_generator_asend(self, value):
    """Send a value into the asynchronous generator.
    Return next yielded value or raise StopAsyncIteration.
    """
    raise StopAsyncIteration


@_define("AsyncGenerator", "athrow", abstract=True)
async def _async_generator_athrow(self, typ, val=None, tb=None):
    """Raise an exception in the asynchronous generator.
    Return next yielded value or raise StopAsyncIteration.
    """
    if val is None:
        if tb is None:
            raise typ
        val = typ()
    if tb is not None:
        val = val.with_traceback(tb)
    raise val


@_define("AsyncGenerator", "aclose")
async def _async_generator_aclose(self):
    """Raise GeneratorExit inside coroutine.
    """
    try:
        await self.athrow(GeneratorExit)
    except (GeneratorExit, StopAsyncIteration):
        pass
    else:
        raise RuntimeError("asynchronous generator ignored GeneratorExit")


@_define("Iterable", "__iter__", abstract=True)
def _iterable_iter(self):
    while False:
        yield None


@_define("Iterator", "__next__", abstract=True)
def _iterator_next(self):
    'Return the next item from the iterator. When exhausted, raise StopIteration'
    raise StopIteration


@_define("Iterator", "__iter__")
def _iterator_iter(self):
    return self


@_define("Reversible", "__reversed__", abstract=True)
def _reversible_reversed(self):
    while False:
        yield None


@_define("Generator", "__next__")
def _generator_next(self):
    """Return the next item from the generator.
    When exhausted, raise StopIteration.
    """
    return self.send(None)


@_define("Generator", "send", abstract=True)
def _generator_send(self, value):
    """Send a value into the generator.
    Return next yielded value or raise StopIteration.
    """
    raise StopIteration


@_define("Generator", "throw", abstract=True)
def _generator_throw(self, typ, val=None, tb=None):
    """Raise an exception in the generator.
    Return next yielded value or raise StopIteration.
    """
    if val is None:
        if tb is None:
            raise typ
        val = typ()
    if tb is not None:
        val = val.with_traceback(tb)
    raise val


@_define("Generator", "close")
def _generator_close(self):
    """Raise GeneratorExit inside generator.
    """
    try:
        self.throw(GeneratorExit)
    except (GeneratorExit, StopIteration):
        pass
    else:
        raise RuntimeError("generator ignored GeneratorExit")


@_define("Sized", "__len__", abstract=True)
def _sized_len(self):
    return 0


@_define("Container", "__contains__", abstract=True)
def _container_contains(self, x):
    return False


@_define("Buffer", "__buffer__", abstract=True)
def _buffer_buffer(self, flags, /):
    raise NotImplementedError


@_define("Callable", "__call__", abstract=True)
def _callable_call(self, *args, **kwds):
    return False


### SETS ###

@_define("Set", "__le__")
def _set_le(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    if len(self) > len(other):
        return False
    for elem in self:
        if elem not in other:
            return False
    return True


@_define("Set", "__lt__")
def _set_lt(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    return len(self) < len(other) and self.__le__(other)


@_define("Set", "__gt__")
def _set_gt(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    return len(self) > len(other) and self.__ge__(other)


@_define("Set", "__ge__")
def _set_ge(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    if len(self) < len(other):
        return False
    for elem in other:
        if elem not in self:
            return False
    return True


@_define("Set", "__eq__")
def _set_eq(self, other):
    if not isinstance(other, Set):
        return NotImplemented
    return len(self) == len(other) and self.__le__(other)


@_define("Set", "_from_iterable")
def _set_from_iterable(cls, it):
    '''Construct an instance of the class from any iterable input.

    Must override this method if the class constructor signature
    does not accept an iterable for an input.
    '''
    return cls(it)


@_define("Set", "__and__")
def _set_and(self, other):
    if not isinstance(other, Iterable):
        return NotImplemented
    return self._from_iterable(value for value in other if value in self)


_alias("Set", "__rand__", "__and__")


@_define("Set", "isdisjoint")
def _set_isdisjoint(self, other):
    'Return True if two sets have a null intersection.'
    for value in other:
        if value in self:
            return False
    return True


@_define("Set", "__or__")
def _set_or(self, other):
    if not isinstance(other, Iterable):
        return NotImplemented
    chain = (e for s in (self, other) for e in s)
    return self._from_iterable(chain)


_alias("Set", "__ror__", "__or__")


@_define("Set", "__sub__")
def _set_sub(self, other):
    if not isinstance(other, Set):
        if not isinstance(other, Iterable):
            return NotImplemented
        other = self._from_iterable(other)
    return self._from_iterable(value for value in self
                               if value not in other)


@_define("Set", "__rsub__")
def _set_rsub(self, other):
    if not isinstance(other, Set):
        if not isinstance(other, Iterable):
            return NotImplemented
        other = self._from_iterable(other)
    return self._from_iterable(value for value in other
                               if value not in self)


@_define("Set", "__xor__")
def _set_xor(self, other):
    if not isinstance(other, Set):
        if not isinstance(other, Iterable):
            return NotImplemented
        other = self._from_iterable(other)
    return (self - other) | (other - self)


_alias("Set", "__rxor__", "__xor__")


@_define("Set", "_hash")
def _set_hash(self):
    """Compute the hash value of a set.

    Note that we don't define __hash__: not all sets are hashable.
    But if you define a hashable set type, its __hash__ should
    call this function.

    This must be compatible __eq__.

    All sets ought to compare equal if they contain the same
    elements, regardless of how they are implemented, and
    regardless of the order of the elements; so there's not much
    freedom for __eq__ or __hash__.  We match the algorithm used
    by the built-in frozenset type.
    """
    MAX = sys.maxsize
    MASK = 2 * MAX + 1
    n = len(self)
    h = 1927868237 * (n + 1)
    h &= MASK
    for x in self:
        hx = hash(x)
        h ^= (hx ^ (hx << 16) ^ 89869747)  * 3644798167
        h &= MASK
    h ^= (h >> 11) ^ (h >> 25)
    h = h * 69069 + 907133923
    h &= MASK
    if h > MAX:
        h -= MASK + 1
    if h == -1:
        h = 590923713
    return h


@_define("MutableSet", "add", abstract=True)
def _mutable_set_add(self, value):
    """Add an element."""
    raise NotImplementedError


@_define("MutableSet", "discard", abstract=True)
def _mutable_set_discard(self, value):
    """Remove an element.  Do not raise an exception if absent."""
    raise NotImplementedError


@_define("MutableSet", "remove")
def _mutable_set_remove(self, value):
    """Remove an element. If not a member, raise a KeyError."""
    if value not in self:
        raise KeyError(value)
    self.discard(value)


@_define("MutableSet", "pop")
def _mutable_set_pop(self):
    """Return the popped value.  Raise KeyError if empty."""
    it = iter(self)
    try:
        value = next(it)
    except StopIteration:
        raise KeyError from None
    self.discard(value)
    return value


@_define("MutableSet", "clear")
def _mutable_set_clear(self):
    """This is slow (creates N new iterators!) but effective."""
    try:
        while True:
            self.pop()
    except KeyError:
        pass


@_define("MutableSet", "__ior__")
def _mutable_set_ior(self, it):
    for value in it:
        self.add(value)
    return self


@_define("MutableSet", "__iand__")
def _mutable_set_iand(self, it):
    for value in (self - it):
        self.discard(value)
    return self


@_define("MutableSet", "__ixor__")
def _mutable_set_ixor(self, it):
    if it is self:
        self.clear()
    else:
        if not isinstance(it, Set):
            it = self._from_iterable(it)
        for value in it:
            if value in self:
                self.discard(value)
            else:
                self.add(value)
    return self


@_define("MutableSet", "__isub__")
def _mutable_set_isub(self, it):
    if it is self:
        self.clear()
    else:
        for value in it:
            self.discard(value)
    return self


### MAPPINGS ###

@_define("Mapping", "__getitem__", abstract=True)
def _mapping_getitem(self, key):
    raise KeyError


@_define("Mapping", "get")
def _mapping_get(self, key, default=None):
    'D.get(k[,d]) -> D[k] if k in D, else d.  d defaults to None.'
    try:
        return self[key]
    except KeyError:
        return default


@_define("Mapping", "__contains__")
def _mapping_contains(self, key):
    try:
        self[key]
    except KeyError:
        return False
    else:
        return True


@_define("Mapping", "keys")
def _mapping_keys(self):
    "D.keys() -> a set-like object providing a view on D's keys"
    return KeysView(self)


@_define("Mapping", "items")
def _mapping_items(self):
    "D.items() -> a set-like object providing a view on D's items"
    return ItemsView(self)


@_define("Mapping", "values")
def _mapping_values(self):
    "D.values() -> an object providing a view on D's values"
    return ValuesView(self)


@_define("Mapping", "__eq__")
def _mapping_eq(self, other):
    if not isinstance(other, Mapping):
        return NotImplemented
    return dict(self.items()) == dict(other.items())


@_define("MappingView", "__init__")
def _mapping_view_init(self, mapping):
    self._mapping = mapping


@_define("MappingView", "__len__")
def _mapping_view_len(self):
    return len(self._mapping)


@_define("MappingView", "__repr__")
def _mapping_view_repr(self):
    return '{0.__class__.__name__}({0._mapping!r})'.format(self)


@_define("KeysView", "_from_iterable")
def _keys_view_from_iterable(cls, it):
    return set(it)


@_define("KeysView", "__contains__")
def _keys_view_contains(self, key):
    return key in self._mapping


@_define("KeysView", "__iter__")
def _keys_view_iter(self):
    yield from self._mapping


@_define("ItemsView", "_from_iterable")
def _items_view_from_iterable(cls, it):
    return set(it)


@_define("ItemsView", "__contains__")
def _items_view_contains(self, item):
    key, value = item
    try:
        v = self._mapping[key]
    except KeyError:
        return False
    else:
        return v is value or v == value


@_define("ItemsView", "__iter__")
def _items_view_iter(self):
    for key in self._mapping:
        yield (key, self._mapping[key])


@_define("ValuesView", "__contains__")
def _values_view_contains(self, value):
    for key in self._mapping:
        v = self._mapping[key]
        if v is value or v == value:
            return True
    return False


@_define("ValuesView", "__iter__")
def _values_view_iter(self):
    for key in self._mapping:
        yield self._mapping[key]


@_define("MutableMapping", "__setitem__", abstract=True)
def _mutable_mapping_setitem(self, key, value):
    raise KeyError


@_define("MutableMapping", "__delitem__", abstract=True)
def _mutable_mapping_delitem(self, key):
    raise KeyError


# CPython's MutableMapping.__marker.
_marker = object()


@_define("MutableMapping", "pop")
def _mutable_mapping_pop(self, key, default=_marker):
    '''D.pop(k[,d]) -> v, remove specified key and return the corresponding
    value.  If key is not found, d is returned if given, otherwise
    KeyError is raised.
    '''
    try:
        value = self[key]
    except KeyError:
        if default is _marker:
            raise
        return default
    else:
        del self[key]
        return value


@_define("MutableMapping", "popitem")
def _mutable_mapping_popitem(self):
    '''D.popitem() -> (k, v), remove and return some (key, value) pair
       as a 2-tuple; but raise KeyError if D is empty.
    '''
    try:
        key = next(iter(self))
    except StopIteration:
        raise KeyError from None
    value = self[key]
    del self[key]
    return key, value


@_define("MutableMapping", "clear")
def _mutable_mapping_clear(self):
    'D.clear() -> None.  Remove all items from D.'
    try:
        while True:
            self.popitem()
    except KeyError:
        pass


@_define("MutableMapping", "update")
def _mutable_mapping_update(self, other=(), /, **kwds):
    ''' D.update([E, ]**F) -> None.  Update D from mapping/iterable E and F.
        If E present and has a .keys() method, does:
            for k in E.keys(): D[k] = E[k]
        If E present and lacks .keys() method, does:
            for (k, v) in E: D[k] = v
        In either case, this is followed by:
            for k, v in F.items(): D[k] = v
    '''
    if isinstance(other, Mapping):
        for key in other:
            self[key] = other[key]
    elif hasattr(other, "keys"):
        for key in other.keys():
            self[key] = other[key]
    else:
        for key, value in other:
            self[key] = value
    for key, value in kwds.items():
        self[key] = value


@_define("MutableMapping", "setdefault")
def _mutable_mapping_setdefault(self, key, default=None):
    'D.setdefault(k[,d]) -> D.get(k,d), also set D[k]=d if k not in D'
    try:
        return self[key]
    except KeyError:
        self[key] = default
    return default


### SEQUENCES ###

@_define("Sequence", "__getitem__", abstract=True)
def _sequence_getitem(self, index):
    raise IndexError


@_define("Sequence", "__iter__")
def _sequence_iter(self):
    i = 0
    try:
        while True:
            v = self[i]
            yield v
            i += 1
    except IndexError:
        return


@_define("Sequence", "__contains__")
def _sequence_contains(self, value):
    for v in self:
        if v is value or v == value:
            return True
    return False


@_define("Sequence", "__reversed__")
def _sequence_reversed(self):
    for i in reversed(range(len(self))):
        yield self[i]


@_define("Sequence", "index")
def _sequence_index(self, value, start=0, stop=None):
    '''S.index(value, [start, [stop]]) -> integer -- return first index of
       value.  Raises ValueError if the value is not present.

       Supporting start and stop arguments is optional, but
       recommended.
    '''
    if start is not None and start < 0:
        start = max(len(self) + start, 0)
    if stop is not None and stop < 0:
        stop += len(self)

    i = start
    while stop is None or i < stop:
        try:
            v = self[i]
        except IndexError:
            break
        if v is value or v == value:
            return i
        i += 1
    raise ValueError


@_define("Sequence", "count")
def _sequence_count(self, value):
    'S.count(value) -> integer -- return number of occurrences of value'
    return sum(1 for v in self if v is value or v == value)


@_define("MutableSequence", "__setitem__", abstract=True)
def _mutable_sequence_setitem(self, index, value):
    raise IndexError


@_define("MutableSequence", "__delitem__", abstract=True)
def _mutable_sequence_delitem(self, index):
    raise IndexError


@_define("MutableSequence", "insert", abstract=True)
def _mutable_sequence_insert(self, index, value):
    'S.insert(index, value) -- insert value before index'
    raise IndexError


@_define("MutableSequence", "append")
def _mutable_sequence_append(self, value):
    'S.append(value) -- append value to the end of the sequence'
    self.insert(len(self), value)


@_define("MutableSequence", "clear")
def _mutable_sequence_clear(self):
    'S.clear() -> None -- remove all items from S'
    try:
        while True:
            self.pop()
    except IndexError:
        pass


@_define("MutableSequence", "reverse")
def _mutable_sequence_reverse(self):
    'S.reverse() -- reverse *IN PLACE*'
    n = len(self)
    for i in range(n//2):
        self[i], self[n-i-1] = self[n-i-1], self[i]


@_define("MutableSequence", "extend")
def _mutable_sequence_extend(self, values):
    """S.extend(iterable) -- extend sequence by appending elements from the
    iterable"""
    if values is self:
        values = list(values)
    for v in values:
        self.append(v)


@_define("MutableSequence", "pop")
def _mutable_sequence_pop(self, index=-1):
    '''S.pop([index]) -> item -- remove and return item at index (default
    last).  Raise IndexError if list is empty or index is out of range.
    '''
    v = self[index]
    del self[index]
    return v


@_define("MutableSequence", "remove")
def _mutable_sequence_remove(self, value):
    '''S.remove(value) -- remove first occurrence of value.
       Raise ValueError if the value is not present.
    '''
    del self[self.index(value)]


@_define("MutableSequence", "__iadd__")
def _mutable_sequence_iadd(self, values):
    self.extend(values)
    return self


# The class methods of the ABCs.
_CLASS_METHODS = ("_from_iterable",)

# Attributes CPython's class bodies set to None: Mapping is not reversible,
# and a class defining __eq__ without __hash__ is unhashable.
_NONE_ATTRIBUTES = {
    "Set": ("__hash__",),
    "Mapping": ("__hash__", "__reversed__"),
}


def _update_abstractmethods(cls):
    """Set cls.__abstractmethods__ as ABCMeta.__new__ (abc._abc_init) does:
    the names cls defines as abstract, and those of its bases it does not
    override with a concrete attribute."""
    abstracts = set()
    for name, value in vars(cls).items():
        if getattr(value, "__isabstractmethod__", False):
            abstracts.add(name)
    for base in cls.__bases__:
        for name in getattr(base, "__abstractmethods__", ()):
            value = getattr(cls, name, None)
            if getattr(value, "__isabstractmethod__", False):
                abstracts.add(name)
    cls.__abstractmethods__ = frozenset(abstracts)


def _abc_init_subclass(owner):
    """An __init_subclass__ for the root ABC `owner`.

    The native ABCs have no ABCMeta, whose __new__ computes the abstract
    methods of every class derived from an ABC; this hook does it instead.
    It continues the cooperative chain past `owner`.  A class whose own
    __init_subclass__ does not call super() leaves its subclasses without
    __abstractmethods__ of their own, which only makes them instantiable.
    """
    def __init_subclass__(cls, /, **kwargs):
        _update_abstractmethods(cls)
        super(owner, cls).__init_subclass__(**kwargs)
    __init_subclass__.__qualname__ = owner.__name__ + ".__init_subclass__"
    return classmethod(__init_subclass__)


# CPython's registrations of built-in types that the native module does not
# make itself (it registers dict, list, tuple, str, bytes, set, frozenset,
# range, memoryview, bytearray, mappingproxy, generator, coroutine and
# async_generator).  longrange_iterator is left out: range(1 << 1000) needs
# a big-integer range.  A type that reads as `object` (the range iterator's
# does) is skipped: registering it would make every class an Iterator.
def _register_builtin_types():
    registrations = (
        (Iterator, iter(b'')),
        (Iterator, iter(bytearray())),
        (Iterator, iter({}.keys())),
        (Iterator, iter({}.values())),
        (Iterator, iter({}.items())),
        (Iterator, iter([])),
        (Iterator, iter(reversed([]))),
        (Iterator, iter(range(0))),
        (Iterator, iter(set())),
        (Iterator, iter("")),
        (Iterator, iter(())),
        (Iterator, iter(zip())),
        (KeysView, {}.keys()),
        (ValuesView, {}.values()),
        (ItemsView, {}.items()),
    )
    for abc, sample in registrations:
        if type(sample) is not object:
            abc.register(type(sample))


def install(module):
    """Attach the methods to the ABCs of the native `module`.

    The ABCs are visited in __mro__-compatible order (every base before its
    subclasses), so each one's __abstractmethods__ can be computed from its
    bases' as ABCMeta.__new__ does.
    """
    g = globals()
    done = set()

    def visit(cls):
        if cls in done or cls is object:
            return
        for base in cls.__bases__:
            visit(base)
        done.add(cls)
        name = cls.__name__
        # Lib/_collections_abc.py runs as `collections.abc`.
        cls.__module__ = "collections.abc"
        cls.__qualname__ = name
        for attr, func in _METHODS.get(name, {}).items():
            func.__module__ = "collections.abc"
            setattr(cls, attr, classmethod(func) if attr in _CLASS_METHODS else func)
        for attr in _NONE_ATTRIBUTES.get(name, ()):
            setattr(cls, attr, None)
        if cls.__bases__ == (object,):
            cls.__init_subclass__ = _abc_init_subclass(cls)
        _update_abstractmethods(cls)

    names = [name for name in module.__all__
             if not name.startswith("_") and name != "ABCMeta"]
    for name in names:
        g[name] = getattr(module, name)
    for name in names:
        visit(g[name])
    _register_builtin_types()
