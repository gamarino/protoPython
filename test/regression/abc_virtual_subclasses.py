# isinstance / issubclass against the collections.abc ABCs: CPython's
# registrations of the built-in types, the ABC hierarchy, the one-method
# subclass hooks, register(), and classes deriving from an ABC.
# abc_virtual_subclasses.expected is CPython's output.
import collections.abc as A

checks = [
    ({}, "Mapping"), ({}, "MutableMapping"), ({}, "Sequence"), ("", "Mapping"),
    ([], "Sequence"), ([], "MutableSequence"), ((), "Sequence"), ((), "MutableSequence"),
    ("", "Sequence"), (b"", "Sequence"), (range(3), "Sequence"),
    (set(), "Set"), (set(), "MutableSet"), (frozenset(), "Set"), (frozenset(), "MutableSet"),
    ([], "Iterable"), (3, "Iterable"), ([], "Hashable"), (3, "Hashable"), ({}, "Hashable"),
    (lambda: 0, "Callable"), (3, "Callable"), (int, "Callable"),
    (iter([]), "Iterator"), ([], "Iterator"), ({}, "Sized"), ({}, "Collection"),
    ([], "Reversible"), ({}, "Container"), ((x for x in []), "Generator"),
    ((x for x in []), "Iterator"),
]
for obj, name in checks:
    print(type(obj).__name__, name, isinstance(obj, getattr(A, name)))

print("issubclass", issubclass(dict, A.Mapping), issubclass(list, A.Mapping),
      issubclass(dict, A.Iterable), issubclass(tuple, A.Collection))

class MyMap(A.Mapping):
    def __init__(self):
        self.d = {"a": 1}
    def __getitem__(self, k):
        return self.d[k]
    def __len__(self):
        return len(self.d)
    def __iter__(self):
        return iter(self.d)

m = MyMap()
print("subclass", isinstance(m, A.Mapping), isinstance(m, MyMap), isinstance({}, MyMap),
      A.Mapping in MyMap.__mro__, dict(m.items()), m.get("a"), "a" in m)

class Virtual:
    pass

print("before register", isinstance(Virtual(), A.Sequence))
A.Sequence.register(Virtual)
print("after register", isinstance(Virtual(), A.Sequence), issubclass(Virtual, A.Collection),
      isinstance(Virtual(), A.MutableSequence))

class Sized:
    def __len__(self):
        return 0

class NotHashable:
    __hash__ = None

print("hooks", isinstance(Sized(), A.Sized), isinstance(Sized(), A.Iterable),
      isinstance(NotHashable(), A.Hashable))
