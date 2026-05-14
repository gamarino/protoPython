"""Helper to provide extensibility for pickle.

This is only useful to add pickle support for extension types defined in
C, not for instances of user-defined classes.
"""

__all__ = ["pickle", "constructor",
           "add_extension", "remove_extension", "clear_extension_cache"]

dispatch_table = {}

def pickle(ob_type, pickle_function, constructor_ob=None):
    if not callable(pickle_function):
        raise TypeError("reduction functions must be callable")
    dispatch_table[ob_type] = pickle_function

    # The constructor_ob function is a vestige of safe for unpickling.
    # There is no reason for the caller to pass it anymore.
    if constructor_ob is not None:
        constructor(constructor_ob)

def constructor(object):
    if not callable(object):
        raise TypeError("constructors must be callable")

# Example: provide pickling support for complex numbers.

def pickle_complex(c):
    return complex, (c.real, c.imag)

pickle(complex, pickle_complex, complex)

def pickle_union(obj):
    import typing, operator
    return operator.getitem, (typing.Union, obj.__args__)

pickle(type(int | str), pickle_union)

def pickle_super(obj):
    return super, (obj.__thisclass__, obj.__self__)

pickle(super, pickle_super)

# Support for pickling new-style objects

def _reconstructor(cls, base, state):
    if base is object:
        obj = object.__new__(cls)
    else:
        obj = base.__new__(cls, state)
        if base.__init__ != object.__init__:
            base.__init__(obj, state)
    return obj

_HEAPTYPE = 1<<9
_new_type = type(int.__new__)

# Python code for object.__reduce_ex__ for protocols 0 and 1

def _reduce_ex(self, proto):
    # CPython routes protocol >= 2 through a dedicated C-level path on
    # object.__reduce_ex__; protoPython's py_object_reduce_ex delegates
    # everything here.  For proto >= 2 we mirror that C path so the
    # reduction shape matches: a 5-tuple of
    # (__newobj__ / __newobj_ex__, (cls, *args) or (cls, args, kwargs),
    #  state, listitems, dictitems).  For proto < 2 fall through to the
    # legacy 3/4-tuple `(cls, base, state)` shape.
    cls = self.__class__
    if proto >= 2:
        # Resolve newargs via __getnewargs_ex__ / __getnewargs__.  Per
        # PEP 307, __getnewargs_ex__ takes precedence and selects the
        # __newobj_ex__ flavour; __getnewargs__ goes through __newobj__.
        getnewargs_ex = getattr(self, "__getnewargs_ex__", None)
        if getnewargs_ex is not None:
            newargs = getnewargs_ex()
            if (not isinstance(newargs, tuple)
                or len(newargs) != 2
                or not isinstance(newargs[0], tuple)
                or not isinstance(newargs[1], dict)):
                raise TypeError(
                    f"__getnewargs_ex__ should return a tuple of "
                    f"(args, kwargs), not {type(newargs).__name__}")
            args, kwargs = newargs
            if kwargs:
                func = __newobj_ex__
                func_args = (cls, args, kwargs)
            else:
                func = __newobj__
                func_args = (cls,) + args
        else:
            getnewargs = getattr(self, "__getnewargs__", None)
            if getnewargs is not None:
                args = getnewargs()
                if not isinstance(args, tuple):
                    raise TypeError(
                        f"__getnewargs__ should return a tuple, "
                        f"not {type(args).__name__}")
            else:
                args = ()
            func = __newobj__
            func_args = (cls,) + args
        # state: __getstate__() if defined, else __dict__ / None.
        # Crucial: do NOT normalise an empty-dict user-defined
        # __getstate__ to None.  CPython's object.__getstate__ handles
        # the "no state" → None mapping internally and returns None when
        # both __dict__ and slots are empty; a user that explicitly
        # returns `{}` is intentionally signalling "empty dict state",
        # not "no state".  test_descr.PicklingTests.test_special_method_lookup
        # has a Picky.__getstate__ returning `{}` and asserts
        # `reduce_value[2] == {}` precisely to catch this distinction.
        getstate = getattr(self, "__getstate__", None)
        if getstate is not None:
            state = getstate()
        else:
            try:
                state = self.__dict__
            except AttributeError:
                state = None
            if not state:
                state = None
        # listitems / dictitems: per CPython, list and dict subclasses
        # expose their iteration so the unpickler can rebuild the
        # container's payload after constructing the bare instance.
        # Use an explicit MRO walk rather than isinstance() because the
        # latter reports protoPython instances of unrelated classes as
        # dict-compatible (their internal __data__ shape collides with
        # dict's runtime check).
        _mro = type(self).__mro__
        listitems = iter(self) if any(c is list for c in _mro) else None
        dictitems = iter(self.items()) if any(c is dict for c in _mro) else None
        return func, func_args, state, listitems, dictitems
    # Legacy proto<2 path.
    # CPython picks `base` by walking the MRO until it hits a non-heap
    # type (the first C-level class) using __flags__ & _HEAPTYPE.
    # protoPython classes don't expose __flags__, so fall through every
    # iteration and lock onto `object`.  Use `cls.__base__` directly: it
    # already names the immediate non-mixin base, which matches CPython's
    # MRO termination for the typical `class X(BuiltinType):` shape that
    # PicklingTests / pickle round-trips exercise.
    base = cls.__base__ if cls is not object else object
    if base is None:
        base = object
    if base is object:
        state = None
    else:
        if base is cls:
            raise TypeError(f"cannot pickle {cls.__name__!r} object")
        state = base(self)
    args = (cls, base, state)
    try:
        getstate = self.__getstate__
    except AttributeError:
        if getattr(self, "__slots__", None):
            raise TypeError(f"cannot pickle {cls.__name__!r} object: "
                            f"a class that defines __slots__ without "
                            f"defining __getstate__ cannot be pickled "
                            f"with protocol {proto}") from None
        try:
            state_dict = self.__dict__
        except AttributeError:
            state_dict = None
    else:
        if (type(self).__getstate__ is object.__getstate__ and
            getattr(self, "__slots__", None)):
            raise TypeError("a class that defines __slots__ without "
                            "defining __getstate__ cannot be pickled")
        state_dict = getstate()
    # protoPython quirk: for container subclasses (dict / list / set),
    # `object.__getstate__()` returns the container's CONTENTS (not just
    # the instance-attribute dict), and `base(self)` already encodes the
    # same contents into the reconstructor args.  Suppress the duplicate
    # state to match CPython's reduce shape.
    if state_dict is not None and state == state_dict:
        state_dict = None
    if state_dict:
        return _reconstructor, args, state_dict
    else:
        return _reconstructor, args

# Helper for __reduce_ex__ protocol 2

def __newobj__(cls, *args):
    return cls.__new__(cls, *args)

def __newobj_ex__(cls, args, kwargs):
    """Used by pickle protocol 4, instead of __newobj__ to allow classes with
    keyword-only arguments to be pickled correctly.
    """
    return cls.__new__(cls, *args, **kwargs)

def _slotnames(cls):
    """Return a list of slot names for a given class.

    This needs to find slots defined by the class and its bases, so we
    can't simply return the __slots__ attribute.  We must walk down
    the Method Resolution Order and concatenate the __slots__ of each
    class found there.  (This assumes classes don't modify their
    __slots__ attribute to misrepresent their slots after the class is
    defined.)
    """

    # Get the value from a cache in the class if possible
    names = cls.__dict__.get("__slotnames__")
    if names is not None:
        return names

    # Not cached -- calculate the value
    names = []
    if not hasattr(cls, "__slots__"):
        # This class has no slots
        pass
    else:
        # Slots found -- gather slot names from all base classes
        for c in cls.__mro__:
            if "__slots__" in c.__dict__:
                slots = c.__dict__['__slots__']
                # if class has a single slot, it can be given as a string
                if isinstance(slots, str):
                    slots = (slots,)
                for name in slots:
                    # special descriptors
                    if name in ("__dict__", "__weakref__"):
                        continue
                    # mangled names
                    elif name.startswith('__') and not name.endswith('__'):
                        stripped = c.__name__.lstrip('_')
                        if stripped:
                            names.append('_%s%s' % (stripped, name))
                        else:
                            names.append(name)
                    else:
                        names.append(name)

    # Cache the outcome in the class if at all possible
    try:
        cls.__slotnames__ = names
    except:
        pass # But don't die if we can't

    return names

# A registry of extension codes.  This is an ad-hoc compression
# mechanism.  Whenever a global reference to <module>, <name> is about
# to be pickled, the (<module>, <name>) tuple is looked up here to see
# if it is a registered extension code for it.  Extension codes are
# universal, so that the meaning of a pickle does not depend on
# context.  (There are also some codes reserved for local use that
# don't have this restriction.)  Codes are positive ints; 0 is
# reserved.

_extension_registry = {}                # key -> code
_inverted_registry = {}                 # code -> key
_extension_cache = {}                   # code -> object
# Don't ever rebind those names:  pickling grabs a reference to them when
# it's initialized, and won't see a rebinding.

def add_extension(module, name, code):
    """Register an extension code."""
    code = int(code)
    if not 1 <= code <= 0x7fffffff:
        raise ValueError("code out of range")
    key = (module, name)
    if (_extension_registry.get(key) == code and
        _inverted_registry.get(code) == key):
        return # Redundant registrations are benign
    if key in _extension_registry:
        raise ValueError("key %s is already registered with code %s" %
                         (key, _extension_registry[key]))
    if code in _inverted_registry:
        raise ValueError("code %s is already in use for key %s" %
                         (code, _inverted_registry[code]))
    _extension_registry[key] = code
    _inverted_registry[code] = key

def remove_extension(module, name, code):
    """Unregister an extension code.  For testing only."""
    key = (module, name)
    if (_extension_registry.get(key) != code or
        _inverted_registry.get(code) != key):
        raise ValueError("key %s is not registered with code %s" %
                         (key, code))
    del _extension_registry[key]
    del _inverted_registry[code]
    if code in _extension_cache:
        del _extension_cache[code]

def clear_extension_cache():
    _extension_cache.clear()

# Standard extension code assignments

# Reserved ranges

# First  Last Count  Purpose
#     1   127   127  Reserved for Python standard library
#   128   191    64  Reserved for Zope
#   192   239    48  Reserved for 3rd parties
#   240   255    16  Reserved for private use (will never be assigned)
#   256   Inf   Inf  Reserved for future assignment

# Extension codes are assigned by the Python Software Foundation.
