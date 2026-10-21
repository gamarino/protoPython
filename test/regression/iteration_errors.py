# Errors raised while builtins iterate propagate as in CPython: zip, map,
# enumerate, filter, reversed, the consumers of an iterable, empty pops,
# re's Match.group with several groups, configparser.

def t(name, f):
    try:
        r = f()
        print(name, "->", repr(r))
    except Exception as e:
        print(name, "raised", type(e).__name__, e)

class BadIt:
    def __init__(self): self.i = 0
    def __iter__(self): return self
    def __next__(self):
        self.i += 1
        if self.i > 2: raise ValueError("boom")
        return self.i

t("zip_err", lambda: list(zip(BadIt(), [1, 2, 3, 4])))
t("zip_err2", lambda: list(zip([1, 2, 3, 4], BadIt())))
t("zip_strict", lambda: list(zip([1], [1, 2], strict=True)))
t("map_err", lambda: list(map(lambda x: x, BadIt())))
t("enumerate_err", lambda: list(enumerate(BadIt())))
t("filter_err", lambda: list(filter(None, BadIt())))
t("sum_err", lambda: sum(BadIt()))
t("sorted_err", lambda: sorted(BadIt()))
t("tuple_err", lambda: tuple(BadIt()))
t("set_err", lambda: set(BadIt()))
t("dict_fromkeys_err", lambda: dict.fromkeys(BadIt()))
t("any_err", lambda: any(x > 5 for x in BadIt()))
t("join_err", lambda: ",".join(str(x) for x in BadIt()))

class BadLen:
    def __len__(self): raise ValueError("len boom")
    def __getitem__(self, i): return i
t("reversed_badlen", lambda: list(reversed(BadLen())))
class BadGet:
    def __len__(self): return 3
    def __getitem__(self, i):
        if i == 1: raise ValueError("get boom")
        return i
t("reversed_badget", lambda: list(reversed(BadGet())))
class IdxSeq:
    def __len__(self): return 3
    def __getitem__(self, i):
        if i >= 2: raise IndexError
        return i
t("reversed_idx", lambda: list(reversed(IdxSeq())))
t("iter_getitem_err", lambda: list(iter(BadGet())))

t("popitem_empty", lambda: {}.popitem())
t("set_pop_empty", lambda: set().pop())
t("list_pop_empty", lambda: [].pop())
import re, configparser
m = re.match(r"(?P<a>\w+)-(\w+)?(x)?", "ab-")
t("group_many", lambda: m.group(1, 2, 3))
t("group_mixed", lambda: m.group("a", 0))
t("group_missing", lambda: m.group(9))
t("group_missing_name", lambda: m.group("zz"))
c = configparser.ConfigParser()
c.read_string("[a]\nx = 1\ny : two\n[b]\nz = 3\n")
t("configparser", lambda: (c.sections(), c["a"]["x"], c.get("a", "y"), c.getint("b", "z")))
