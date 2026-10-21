def abstractmethod(f):
    f.__isabstractmethod__ = True
    return f


def get_cache_token():
    return 0


def _ns_lookup(ns, name):
    # Subscription, not ns.get(): on protoPython's class namespace a body
    # that defines `get` (a Mapping) shadows the dict method.
    try:
        return ns[name]
    except KeyError:
        return None


class ABCMeta(type):
    def __new__(mcls, name, bases, ns, **kw):
        cls = type.__new__(mcls, name, bases, ns)
        # Populate __abstractmethods__ per CPython ABCMeta semantics:
        # collect names whose values declare __isabstractmethod__ across
        # this class's namespace and any abstract names inherited from
        # bases that this class did not override with a concrete impl.
        abstracts = set()
        # Inherited abstracts from bases (subtract concrete overrides).
        for base in bases:
            inherited = getattr(base, "__abstractmethods__", None) or ()
            for n in inherited:
                # Concrete override in cls drops the inherited abstract.
                v = _ns_lookup(ns, n)
                if v is None:
                    v = getattr(cls, n, None)
                if v is None or getattr(v, "__isabstractmethod__", False):
                    abstracts.add(n)
        # Direct abstracts declared in this class body.
        keys = getattr(ns, "__keys__", None) or list(ns)
        for n in keys:
            v = _ns_lookup(ns, n)
            if v is None:
                v = getattr(cls, n, None)
            if getattr(v, "__isabstractmethod__", False):
                abstracts.add(n)
        cls.__abstractmethods__ = frozenset(abstracts)
        return cls

    def register(cls, sub):
        # STRUCT-210: track virtual subclasses in `_abc_registry` so
        # isinstance/issubclass can honour the registration.  CPython
        # carries this in a per-class WeakSet; a plain set is enough
        # for our purposes (the test surface doesn't exercise weakref
        # collection of registered classes).
        try:
            registry = cls.__dict__.get('_abc_registry')
        except Exception:
            registry = None
        if registry is None:
            registry = set()
            try:
                cls._abc_registry = registry
            except Exception:
                pass
        registry.add(sub)
        return sub

    def __instancecheck__(cls, instance):
        # Check the standard MRO first, then fall back to the
        # registered virtual subclasses.
        if type(instance) is cls:
            return True
        if cls in type(instance).__mro__:
            return True
        return cls.__subclasscheck__(type(instance))

    def __subclasscheck__(cls, subclass):
        if subclass is cls:
            return True
        # Direct MRO check.
        try:
            if cls in subclass.__mro__:
                return True
        except AttributeError:
            pass
        # As CPython's ABCMeta: a class registered with cls (or a subclass
        # of one), or a subclass of one of cls's subclasses -- whose own
        # registrations count, e.g. int registered with numbers.Integral is
        # a numbers.Rational. A registration on one of cls's BASES does not
        # make the class a subclass of cls.
        registry = cls.__dict__.get('_abc_registry')
        if registry:
            for reg in registry:
                if reg is subclass:
                    return True
                try:
                    if reg in subclass.__mro__:
                        return True
                except AttributeError:
                    pass
        try:
            subclasses = type.__subclasses__(cls)
        except Exception:
            subclasses = ()
        for scls in subclasses:
            if scls is not cls and issubclass(subclass, scls):
                return True
        return False


class ABC(metaclass=ABCMeta):
    __slots__ = ()


def update_abstractmethods(c):
    return c
