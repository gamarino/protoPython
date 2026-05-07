"""Stub implementation of the _typing C extension module for protoPython."""

__all__ = [
    '_idfunc', 'TypeVar', 'ParamSpec', 'TypeVarTuple',
    'ParamSpecArgs', 'ParamSpecKwargs', 'TypeAliasType', 'Generic', 'Union',
    'NoDefault',
]


def _idfunc(_, x):
    return x


class _SpecialForm:
    """Base for special typing forms like Union, Optional, etc."""
    __slots__ = ('_name', '__doc__')

    def __init__(self, getitem=None):
        self._name = None

    def __set_name__(self, owner, name):
        self._name = name

    def __repr__(self):
        return 'typing.' + self._name

    def __reduce__(self):
        return self._name

    def __call__(self, *args, **kwargs):
        raise TypeError(f"Cannot instantiate {self!r}")

    def __or__(self, other):
        return _UnionGenericAlias(self, (self, other))

    def __ror__(self, other):
        return _UnionGenericAlias(self, (other, self))


def _normalize_union_args(args):
    """Flatten nested unions and dedupe (CPython semantics for PEP 604).

    `None` literal is rewritten to `type(None)` so `T | None == T | type(None)`.
    """
    result = []
    for a in args:
        if a is None:
            a = type(None)
        # Flatten nested PEP 604 / typing union args.
        nested = getattr(a, '__args__', None)
        is_union = (
            isinstance(a, _UnionGenericAlias)
            or type(a).__name__ == 'UnionType'
        )
        if is_union and nested is not None:
            for sub in nested:
                if sub is None:
                    sub = type(None)
                if sub not in result:
                    result.append(sub)
        else:
            if a not in result:
                result.append(a)
    return tuple(result)


class _UnionGenericAlias:
    def __init__(self, origin, args):
        self.__origin__ = origin
        self.__args__ = _normalize_union_args(args)

    def __repr__(self):
        args = ' | '.join(repr(a) for a in self.__args__)
        return args

    def __getitem__(self, params):
        return _UnionGenericAlias(self.__origin__, self.__args__ + (params,))

    def __or__(self, other):
        return _UnionGenericAlias(self.__origin__, self.__args__ + (other,))

    def __ror__(self, other):
        return _UnionGenericAlias(self.__origin__, (other,) + self.__args__)

    def __eq__(self, other):
        if not isinstance(other, _UnionGenericAlias):
            # Compare against PEP 604 UnionType by __args__ as well.
            if type(other).__name__ == 'UnionType':
                return set(self.__args__) == set(getattr(other, '__args__', ()))
            return NotImplemented
        return set(self.__args__) == set(other.__args__)

    def __hash__(self):
        return hash(frozenset(self.__args__))


# NoDefault sentinel
class _NoDefaultType:
    __instance = None

    def __new__(cls):
        if cls.__instance is None:
            cls.__instance = super().__new__(cls)
        return cls.__instance

    def __repr__(self):
        return 'typing.NoDefault'

    def __bool__(self):
        return False


NoDefault = _NoDefaultType()


class TypeVar:
    """Type variable."""

    def __init__(self, name, *constraints, bound=None, covariant=False,
                 contravariant=False, infer_variance=False, default=NoDefault):
        if not isinstance(name, str):
            raise TypeError("TypeVar() requires a string first argument")
        self.__name__ = name
        self.__bound__ = bound
        self.__constraints__ = tuple(constraints)
        self.__covariant__ = bool(covariant)
        self.__contravariant__ = bool(contravariant)
        self.__infer_variance__ = bool(infer_variance)
        self.__default__ = default
        self.__module__ = None

    def __repr__(self):
        prefix = '~'
        if self.__covariant__:
            prefix = '+'
        elif self.__contravariant__:
            prefix = '-'
        return prefix + self.__name__

    def __typing_subst__(self, arg):
        return arg

    def __or__(self, other):
        return _UnionGenericAlias(Union, (self, other))

    def __ror__(self, other):
        return _UnionGenericAlias(Union, (other, self))

    def has_default(self):
        return self.__default__ is not NoDefault


class ParamSpecArgs:
    """ParamSpec.args attribute."""

    def __init__(self, origin):
        self.__origin__ = origin

    def __repr__(self):
        return f'{self.__origin__.__name__}.args'

    def __eq__(self, other):
        if not isinstance(other, ParamSpecArgs):
            return NotImplemented
        return self.__origin__ == other.__origin__


class ParamSpecKwargs:
    """ParamSpec.kwargs attribute."""

    def __init__(self, origin):
        self.__origin__ = origin

    def __repr__(self):
        return f'{self.__origin__.__name__}.kwargs'

    def __eq__(self, other):
        if not isinstance(other, ParamSpecKwargs):
            return NotImplemented
        return self.__origin__ == other.__origin__


class ParamSpec:
    """Parameter specification variable."""

    def __init__(self, name, *, bound=None, covariant=False,
                 contravariant=False, infer_variance=False, default=NoDefault):
        if not isinstance(name, str):
            raise TypeError("ParamSpec() requires a string first argument")
        self.__name__ = name
        self.__bound__ = bound
        self.__covariant__ = bool(covariant)
        self.__contravariant__ = bool(contravariant)
        self.__infer_variance__ = bool(infer_variance)
        self.__default__ = default

    @property
    def args(self):
        return ParamSpecArgs(self)

    @property
    def kwargs(self):
        return ParamSpecKwargs(self)

    def __repr__(self):
        prefix = '~'
        if self.__covariant__:
            prefix = '+'
        elif self.__contravariant__:
            prefix = '-'
        return prefix + self.__name__

    def __typing_prepare_subst__(self, alias, args):
        return args

    def has_default(self):
        return self.__default__ is not NoDefault


class TypeVarTuple:
    """Type variable tuple for variadic generics."""

    def __init__(self, name, *, default=NoDefault):
        if not isinstance(name, str):
            raise TypeError("TypeVarTuple() requires a string first argument")
        self.__name__ = name
        self.__default__ = default

    def __repr__(self):
        return self.__name__

    def __iter__(self):
        yield self

    @property
    def __typing_unpacked_tuple_args__(self):
        return (self,)

    @property
    def __typing_is_unpacked_typevartuple__(self):
        return True

    def __typing_prepare_subst__(self, alias, args):
        return args

    def has_default(self):
        return self.__default__ is not NoDefault


class TypeAliasType:
    """Type alias created via `type` statement."""

    def __init__(self, name, value, *, type_params=()):
        self.__name__ = name
        self.__value__ = value
        self.__type_params__ = tuple(type_params)

    def __repr__(self):
        return self.__name__

    def __reduce__(self):
        return self.__name__

    @property
    def __value__(self):
        return self.__value__

    def __getitem__(self, params):
        return _GenericAlias(self, params if isinstance(params, tuple) else (params,))


class _GenericAlias:
    def __init__(self, origin, args):
        self.__origin__ = origin
        self.__args__ = args if isinstance(args, tuple) else (args,)

    def __repr__(self):
        args = ', '.join(repr(a) for a in self.__args__)
        return f'{self.__origin__!r}[{args}]'


class _GenericMeta(type):
    def __getitem__(cls, params):
        if not isinstance(params, tuple):
            params = (params,)
        return _GenericAlias(cls, params)


class Generic(metaclass=_GenericMeta):
    """Base class for generic types."""

    __slots__ = ()
    _special = False

    def __class_getitem__(cls, params):
        if not isinstance(params, tuple):
            params = (params,)
        return _GenericAlias(cls, params)

    def __init_subclass__(cls, *args, **kwargs):
        super().__init_subclass__(**kwargs)


class _UnionForm:
    """Special form for Union."""

    def __getitem__(self, params):
        if not isinstance(params, tuple):
            params = (params,)
        return _UnionGenericAlias(self, params)

    def __repr__(self):
        return 'typing.Union'


Union = _UnionForm()
