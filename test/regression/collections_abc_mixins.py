# The methods of the collections.abc ABCs (lib/python3.14/_collections_abc_mixins.py
# installed on the native _collections_abc classes) against CPython: the Set,
# MutableSet, Mapping, MutableMapping, view, Sequence, MutableSequence,
# Iterator, Generator, Coroutine and AsyncGenerator mixins, which exceptions
# they catch and which they let through, the ABC hierarchy, __abstractmethods__
# and abstract instantiation, and classes derived from the ABCs.
# collections_abc_mixins.expected is CPython's output.
import abc as _abc
import collections
import collections.abc as abc


def t(name, f):
    try:
        r = f()
        print(name, "->", repr(r))
    except Exception as e:
        print(name, "raised", type(e).__name__, e)


def t_type(name, f):
    # Only the exception type: the wording of some messages differs.
    try:
        r = f()
        print(name, "->", repr(r))
    except Exception as e:
        print(name, "raised", type(e).__name__)


### Exceptions caught and propagated ###

class M(abc.Mapping):
    def __init__(self, d): self.d = d
    def __getitem__(self, k):
        if k == "bad": raise ValueError("getitem boom")
        return self.d[k]
    def __iter__(self): return iter(self.d)
    def __len__(self): return len(self.d)
m = M({"a": 1})
t("mapping_contains_missing", lambda: "z" in m)
t("mapping_contains_bad", lambda: "bad" in m)
t("mapping_get_missing", lambda: m.get("z", 7))
t("mapping_get_bad", lambda: m.get("bad", 7))
t("mapping_eq", lambda: m == {"a": 1})

class MM(abc.MutableMapping):
    def __init__(self): self.d = {}
    def __getitem__(self, k): return self.d[k]
    def __setitem__(self, k, v): self.d[k] = v
    def __delitem__(self, k):
        if k == "bad": raise ValueError("del boom")
        del self.d[k]
    def __iter__(self): return iter(self.d)
    def __len__(self): return len(self.d)
mm = MM()
t("mm_pop_missing", lambda: mm.pop("z", 5))
t("mm_pop_missing_nodefault", lambda: mm.pop("z"))
t("mm_popitem_empty", lambda: mm.popitem())
mm["bad"] = 1
t("mm_popitem_bad", lambda: mm.popitem())
t("mm_setdefault", lambda: (mm.setdefault("q", 3), mm["q"]))
t("mm_clear_bad", lambda: mm.clear())

class BadSeq(abc.Sequence):
    def __getitem__(self, i):
        if i == 1: raise ValueError("seq boom")
        if i >= 3: raise IndexError
        return i
    def __len__(self): return 3
s = BadSeq()
t("seq_iter_bad", lambda: list(s))
t("seq_contains_bad", lambda: 2 in s)
t("seq_index_bad", lambda: s.index(2))
t("seq_count_bad", lambda: s.count(2))
t("seq_reversed_bad", lambda: list(reversed(s)))
class IdxSeq(abc.Sequence):
    def __getitem__(self, i):
        if i >= 3: raise IndexError
        return i
    def __len__(self): return 3
t("seq_iter_ok", lambda: list(IdxSeq()))
t("seq_index_missing", lambda: IdxSeq().index(9))

class BadSet(abc.MutableSet):
    def __init__(self): self.s = set()
    def __contains__(self, x): return x in self.s
    def __iter__(self): return iter(self.s)
    def __len__(self): return len(self.s)
    def add(self, x): self.s.add(x)
    def discard(self, x):
        if x == "bad": raise ValueError("discard boom")
        self.s.discard(x)
bs = BadSet()
t("ms_pop_empty", lambda: bs.pop())
bs.add("bad")
t("ms_pop_bad", lambda: bs.pop())
t("ms_remove_missing", lambda: bs.remove("zz"))

class ListSeq(abc.MutableSequence):
    def __init__(self, l=()): self.l = list(l)
    def __getitem__(self, i): return self.l[i]
    def __setitem__(self, i, v): self.l[i] = v
    def __delitem__(self, i): del self.l[i]
    def __len__(self): return len(self.l)
    def insert(self, i, v): self.l.insert(i, v)
t("mseq_pop_empty", lambda: ListSeq().pop())
t("mseq_remove_missing", lambda: ListSeq().remove(3))
x = ListSeq(); x.extend([1, 2]); x.reverse()
t("mseq_ops", lambda: list(x))

### Set ###

class S(abc.Set):
    def __init__(self, it=()):
        self.l = []
        for v in it:
            if v not in self.l:
                self.l.append(v)
    def __contains__(self, v): return v in self.l
    def __iter__(self): return iter(self.l)
    def __len__(self): return len(self.l)
    def __repr__(self): return "S(%r)" % sorted(self.l)

a, b = S([1, 2, 3]), S([2, 3, 4])
t("set_and", lambda: a & b)
t("set_or", lambda: a | b)
t("set_sub", lambda: a - b)
t("set_xor", lambda: a ^ b)
t("set_rsub", lambda: [3, 9] - a)
t("set_rand", lambda: {2, 7} & a)
t("set_and_list", lambda: a & [1, 9])
t("set_or_int", lambda: a | 3)
t("set_le", lambda: (S([1]) <= a, a <= S([1]), a <= a))
t("set_lt", lambda: (S([1]) < a, a < a))
t("set_ge", lambda: (a >= S([1]), a >= b))
t("set_gt", lambda: (a > S([1]), a > a))
t("set_eq", lambda: (a == S([3, 2, 1]), a == b, a == {1, 2, 3}, a != b))
t("set_eq_list", lambda: a == [1, 2, 3])
t("set_le_list", lambda: a.__le__([1]))
t("set_isdisjoint", lambda: (a.isdisjoint([7, 8]), a.isdisjoint(b)))
t("set_hash_none", lambda: abc.Set.__hash__)
t("set_hash_call", lambda: hash(a))
t("set__hash", lambda: S([1, 2])._hash() == S([2, 1])._hash())
t("set__hash_value", lambda: S([1, 2, 3])._hash())
t("set_from_iterable", lambda: S._from_iterable([5, 6]))

class FS(abc.Set):
    def __init__(self, it=()): self.s = frozenset(it)
    def __contains__(self, v): return v in self.s
    def __iter__(self): return iter(self.s)
    def __len__(self): return len(self.s)
    __hash__ = abc.Set._hash
t("hashable_set", lambda: hash(FS([1, 2])) == FS([2, 1])._hash())
t("set_dict_key", lambda: {FS([1]): "x"}[FS([1])])

### MutableSet ###

class MS(abc.MutableSet):
    def __init__(self, it=()): self.s = set(it)
    def __contains__(self, v): return v in self.s
    def __iter__(self): return iter(sorted(self.s))
    def __len__(self): return len(self.s)
    def add(self, v): self.s.add(v)
    def discard(self, v): self.s.discard(v)
    def __repr__(self): return "MS(%r)" % sorted(self.s)

def ior():
    v = MS([1]); v |= [2, 3]; return v
def iand():
    v = MS([1, 2, 3]); v &= [2, 3, 4]; return v
def ixor():
    v = MS([1, 2, 3]); v ^= [3, 4]; return v
def ixor_self():
    v = MS([1, 2]); v ^= v; return v
def isub():
    v = MS([1, 2, 3]); v -= [1]; return v
def isub_self():
    v = MS([1, 2, 3]); v -= v; return v
def ms_clear():
    v = MS([1, 2, 3]); v.clear(); return v
def ms_pop():
    v = MS([5]); return (v.pop(), v)
def ms_remove():
    v = MS([5, 6]); v.remove(5); return v
t("ms_ior", ior)
t("ms_iand", iand)
t("ms_ixor", ixor)
t("ms_ixor_self", ixor_self)
t("ms_isub", isub)
t("ms_isub_self", isub_self)
t("ms_clear", ms_clear)
t("ms_pop", ms_pop)
t("ms_remove", ms_remove)
t("ms_or", lambda: MS([1]) | MS([2]))
t("ms_isdisjoint", lambda: MS([1]).isdisjoint([2]))
t("ms_le", lambda: MS([1]) <= MS([1, 2]))
t("ms_mro", lambda: [c.__name__ for c in MS.__mro__])

### Mapping and its views ###

m2 = M({"a": 1, "b": 2})
t("keys_type", lambda: type(m2.keys()).__name__)
t("keys_list", lambda: list(m2.keys()))
t("items_list", lambda: list(m2.items()))
t("values_list", lambda: list(m2.values()))
t("keys_len", lambda: len(m2.keys()))
t("keys_contains", lambda: ("a" in m2.keys(), "z" in m2.keys()))
t("items_contains", lambda: (("a", 1) in m2.items(), ("a", 2) in m2.items(), ("z", 1) in m2.items()))
t("values_contains", lambda: (2 in m2.values(), 5 in m2.values()))
t("keys_and", lambda: sorted(m2.keys() & ["a", "q"]))
t("keys_or", lambda: sorted(m2.keys() | ["q"]))
t("keys_sub", lambda: sorted(m2.keys() - ["a"]))
t("keys_eq", lambda: m2.keys() == {"a", "b"})
t("items_and", lambda: sorted(m2.items() & [("a", 1)]))
t("keys_isinstance", lambda: (isinstance(m2.keys(), abc.Set), isinstance(m2.keys(), abc.KeysView),
                              isinstance(m2.keys(), abc.MappingView), isinstance(m2.values(), abc.Set),
                              isinstance(m2.values(), abc.Collection)))
t("views_from_iterable", lambda: (type(abc.KeysView({"a": 1}) & {"a"}).__name__,
                                  type(abc.ItemsView({"a": 1}) | set()).__name__))
t("valuesview", lambda: (len(abc.ValuesView({"a": 1, "b": 2})), sorted(abc.ValuesView({"a": 1, "b": 2}))))
t("mapping_eq_ne", lambda: (m2 == {"a": 1, "b": 2}, m2 != {"a": 1}, m2 == 5))
t("mapping_hash", lambda: abc.Mapping.__hash__)
t("mapping_reversed_attr", lambda: abc.Mapping.__reversed__)
t("mapping_get", lambda: (m2.get("a"), m2.get("z")))
t("dict_eq_mapping", lambda: {"a": 1, "b": 2} == m2)
t("mapping_dict", lambda: dict(m2))

### MutableMapping ###

class MM2(abc.MutableMapping):
    def __init__(self, *args, **kw):
        self.d = {}
        self.update(*args, **kw)
    def __getitem__(self, k): return self.d[k]
    def __setitem__(self, k, v): self.d[k] = v
    def __delitem__(self, k): del self.d[k]
    def __iter__(self): return iter(self.d)
    def __len__(self): return len(self.d)
class KeysOnly:
    def keys(self): return ["u"]
    def __getitem__(self, k): return k * 2
t("mm_update_kw", lambda: MM2(x=1, y=2).d)
t("mm_update_mapping", lambda: MM2(m2).d)
t("mm_update_pairs", lambda: MM2([("p", 1), ("q", 2)]).d)
t("mm_update_both", lambda: MM2({"k": 1}, z=3).d)
t("mm_update_keys", lambda: MM2(KeysOnly()).d)
def mm_ops():
    v = MM2(a=1, b=2)
    r1 = v.pop("a")
    r2 = v.popitem()
    v["c"] = 3
    v.clear()
    return r1, r2, v.d
t("mm_ops", mm_ops)

### Sequence and MutableSequence ###

class Sq(abc.Sequence):
    def __init__(self, l): self.l = l
    def __getitem__(self, i): return self.l[i]
    def __len__(self): return len(self.l)
q = Sq([1, 2, 3, 2])
t("seq_list", lambda: list(q))
t("seq_reversed", lambda: list(reversed(q)))
t("seq_index", lambda: (q.index(2), q.index(2, 2), q.index(2, -1), q.index(3, 0, 3)))
t("seq_index_stop", lambda: q.index(3, 0, 2))
t("seq_count", lambda: q.count(2))
t("seq_contains", lambda: (2 in q, 9 in q))
t("seq_hash", lambda: abc.Sequence.__hash__ is object.__hash__)
def mseq_ops():
    v = ListSeq([1, 2, 3])
    v.append(4)
    v.reverse()
    p = v.pop()
    p0 = v.pop(0)
    v.remove(2)
    v += [7, 8]
    v.extend(v)
    r = list(v)
    v.clear()
    return p, p0, r, list(v)
t("mseq_ops2", mseq_ops)
t("mseq_index", lambda: ListSeq([5, 6]).index(6))

### Iterator, Generator, Coroutine, AsyncGenerator ###

class It(abc.Iterator):
    def __init__(self): self.i = 0
    def __next__(self):
        self.i += 1
        if self.i > 2: raise StopIteration
        return self.i
t("iterator_iter", lambda: list(It()))
t("iterator_self", lambda: (lambda i: iter(i) is i)(It()))

class Gen(abc.Generator):
    def __init__(self): self.n = 0
    def send(self, v):
        self.n += 1
        if self.n > 3: raise StopIteration
        return self.n
    def throw(self, typ, val=None, tb=None):
        raise typ
g = Gen()
t("gen_next", lambda: (next(g), next(g)))
t("gen_iter", lambda: iter(g) is g)
t("gen_list", lambda: list(Gen()))
t("gen_close", lambda: Gen().close())
class Gen2(Gen):
    def throw(self, typ, val=None, tb=None):
        return 1
t("gen_close_ignored", lambda: Gen2().close())
class Gen3(abc.Generator):
    def send(self, v): return v
    def throw(self, typ, val=None, tb=None):
        return abc.Generator.throw(self, typ, val, tb)
t("gen_throw_default", lambda: Gen3().throw(ValueError))
t("gen_throw_default_val", lambda: Gen3().throw(ValueError, ValueError("v")))

class Co(abc.Coroutine):
    def __await__(self): return iter([])
    def send(self, v): raise StopIteration
    def throw(self, typ, val=None, tb=None): raise typ
t("coro_close", lambda: Co().close())

class AG(abc.AsyncGenerator):
    def __init__(self): self.log = []
    async def asend(self, v):
        self.log.append(("send", v))
        return 1
    async def athrow(self, typ, val=None, tb=None):
        self.log.append(("throw", typ.__name__))
        raise typ
def drive(coro):
    try:
        coro.send(None)
    except StopIteration as e:
        return ("done", e.value)
    return "suspended"
ag = AG()
t("ag_anext", lambda: drive(ag.__anext__()))
t("ag_aclose", lambda: drive(ag.aclose()))
t("ag_log", lambda: ag.log)
t("ag_aiter", lambda: ag.__aiter__() is ag)
class AG2(AG):
    async def athrow(self, typ, val=None, tb=None):
        return 5
t("ag_aclose_ignored", lambda: drive(AG2().aclose()))

### One-trick ponies ###

class Rev(abc.Reversible):
    def __iter__(self): return iter([1, 2])
    def __reversed__(self): return iter([2, 1])
t("reversible", lambda: list(reversed(Rev())))
class Hs(abc.Hashable):
    def __hash__(self): return 42
t("hashable_impl", lambda: hash(Hs()))
class Cal(abc.Callable):
    def __call__(self): return 7
t("callable_impl", lambda: (Cal()(), callable(Cal()), isinstance(Cal(), abc.Callable)))
t("mapping_not_callable", lambda: callable(m))
class Bf:
    def __buffer__(self, flags): return memoryview(b"")
t("buffer_hook", lambda: isinstance(Bf(), abc.Buffer))

### Abstract methods and instantiation ###

t("abstractmethods", lambda: {n: sorted(getattr(abc, n).__abstractmethods__) for n in
    ["Hashable", "Iterable", "Iterator", "Reversible", "Generator", "Sized", "Container",
     "Collection", "Set", "MutableSet", "Mapping", "MutableMapping", "MappingView",
     "KeysView", "ItemsView", "ValuesView", "Sequence", "MutableSequence",
     "Awaitable", "Coroutine", "AsyncIterable", "AsyncIterator", "AsyncGenerator", "Callable"]})
t("mixin_isabstract", lambda: (getattr(abc.Mapping.__getitem__, "__isabstractmethod__", False),
                               getattr(abc.Mapping.get, "__isabstractmethod__", False)))
t_type("inst_mapping", lambda: abc.Mapping())
t_type("inst_sized", lambda: abc.Sized())
class Partial(abc.Mapping):
    def __getitem__(self, k): return 1
t_type("inst_partial", lambda: Partial())
t("partial_abstracts", lambda: sorted(Partial.__abstractmethods__))
t("inst_keysview", lambda: type(abc.KeysView({})).__name__)
for cls in (abc.Hashable, abc.Sized, abc.Container, abc.Iterable, abc.Callable):
    Sub = type("Sub" + cls.__name__, (cls,), {})
    t_type("inst_" + Sub.__name__, Sub)

class Base(abc.Mapping):
    seen = []
    def __init_subclass__(cls, /, tag=None, **kw):
        Base.seen.append((cls.__name__, tag))
        super().__init_subclass__(**kw)
class Impl(Base, tag="x"):
    def __getitem__(self, k): raise KeyError(k)
    def __iter__(self): return iter(())
    def __len__(self): return 0
t("coop_init_subclass", lambda: (Base.seen, len(Impl()), sorted(Impl.__abstractmethods__)))
class Half(Base):
    def __getitem__(self, k): raise KeyError(k)
t_type("coop_half", lambda: Half())
t("half_abstracts", lambda: sorted(Half.__abstractmethods__))
class NoSuper(abc.Sequence):
    def __init_subclass__(cls, **kw):
        pass
class NoSuperImpl(NoSuper):
    def __getitem__(self, i): raise IndexError
    def __len__(self): return 0
t("nosuper_impl", lambda: list(NoSuperImpl()))
t("kw_init_subclass", lambda: type("K", (abc.Sized,), {}, bogus=1))

### Hierarchy, names, registrations ###

t("mros", lambda: {n: [c.__name__ for c in getattr(abc, n).__mro__] for n in
    ["MutableSet", "KeysView", "ItemsView", "ValuesView", "MutableMapping", "MutableSequence",
     "Generator", "Coroutine", "AsyncGenerator", "Collection"]})
t("bases", lambda: {n: [c.__name__ for c in getattr(abc, n).__bases__] for n in
    ["Hashable", "Set", "KeysView", "Sequence", "Collection"]})
t("issub_hier", lambda: (issubclass(abc.MutableSet, abc.Set), issubclass(abc.KeysView, abc.Set),
                         issubclass(abc.Sequence, abc.Reversible), issubclass(abc.Set, abc.MutableSet)))
t("issubclass_chain", lambda: (issubclass(Impl, abc.Mapping), issubclass(Impl, abc.Collection),
                               issubclass(Impl, abc.Sized), isinstance(Impl(), abc.Container),
                               issubclass(Impl, abc.MutableMapping)))
t("registered", lambda: (issubclass(dict, abc.Mapping), issubclass(dict, abc.Sized),
                         issubclass(set, abc.Set), issubclass(list, abc.Sequence),
                         issubclass(list, abc.Reversible), issubclass(str, abc.MutableSequence)))
t("builtins_isinstance", lambda: [isinstance(v, c) for v, c in [
    ({}.keys(), abc.KeysView), ({}.items(), abc.ItemsView), ({}.values(), abc.ValuesView),
    ({}.keys(), abc.Set), ({}, abc.Mapping), ([], abc.MutableSequence), ((), abc.Sequence),
    (set(), abc.MutableSet), (frozenset(), abc.Set), ("", abc.Sequence), (iter(()), abc.Iterator),
    (iter([]), abc.Iterator), (iter(""), abc.Iterator), (iter({}), abc.Iterator),
    ((v for v in ()), abc.Generator), (3, abc.Hashable), ([], abc.Hashable),
    (b"", abc.Sequence), (range(2), abc.Reversible), (3, abc.Iterable), (3, abc.Iterator)]])
t("names", lambda: (abc.Set.__le__.__name__, abc.Mapping.get.__qualname__))
t("module_names", lambda: (abc.Mapping.__module__, abc.Mapping.get.__module__, abc.Mapping.__qualname__))

### Classes deriving from the ABCs ###

class WithABC(_abc.ABC, abc.MutableMapping):
    def __init__(self): self.d = {}
    def __getitem__(self, k): return self.d[k]
    def __setitem__(self, k, v): self.d[k] = v
    def __delitem__(self, k): del self.d[k]
    def __iter__(self): return iter(self.d)
    def __len__(self): return len(self.d)
def with_abc():
    w = WithABC(); w.update(a=1); w.setdefault("b", 2)
    return dict(w), w.pop("a"), w.get("zz", 0), "b" in w
t("with_abc", with_abc)
class Both(dict, abc.MutableMapping):
    pass
t("dict_and_mm", lambda: (Both(a=1).get("a"), [c.__name__ for c in Both.__mro__]))
class Ud(collections.UserDict):
    pass
def userdict():
    u = Ud({"a": 1}, b=2)
    u.update([("c", 3)])
    return (sorted(u.items()), u.pop("a"), u.setdefault("z", 9), sorted(u.keys()),
            list(u.values()), u == {"b": 2, "c": 3, "z": 9})
t("userdict", userdict)
def chainmap():
    c = collections.ChainMap({"a": 1}, {"b": 2})
    return sorted(c.items()), c.get("b"), "a" in c, len(c), sorted(c.keys() | {"q"})
t("chainmap", chainmap)
t("userlist", lambda: (collections.UserList([3, 1, 2]).index(1), collections.UserList([1, 1]).count(1)))
