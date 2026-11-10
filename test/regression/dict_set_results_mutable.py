# Every dict and set a built-in operation produces is mutable. dict | dict
# (and dict.__ror__, which `|=` on a globals copy and dict.fromkeys reached)
# built its result as an immutable object: a write to it was lost silently.
# Each result below is written to and read back.

import collections
fails = []
def chk(name, make, kind):
    try:
        r = make()
    except Exception as e:
        fails.append((name, "raised " + repr(e))); return
    try:
      ok = probe(r, kind)
    except Exception as e:
      fails.append((name, "probe raised " + repr(e))); return
    if not ok: fails.append((name, "write lost"))
def probe(r, kind):
    if kind == "d":
        r["__probe__"] = 1; ok = r.get("__probe__") == 1 and "__probe__" in r
        r["__probe__"] = 2; ok = ok and r["__probe__"] == 2
        del r["__probe__"]; ok = ok and "__probe__" not in r
        r.update({"u": 1}); ok = ok and r["u"] == 1
        r |= {"v": 2}; ok = ok and r["v"] == 2
        r.setdefault("w", 3); ok = ok and r["w"] == 3
        r.pop("w"); ok = ok and "w" not in r
        r.clear(); ok = ok and len(r) == 0
    else:
        r.add("__probe__"); ok = "__probe__" in r
        r.discard("__probe__"); ok = ok and "__probe__" not in r
        r |= {"v"}; ok = ok and "v" in r
        r.update({"w"}); ok = ok and "w" in r
        r -= {"w"}; ok = ok and "w" not in r
        r.clear(); ok = ok and len(r) == 0
    return ok
a = {"a": 1, 5: 2}; b = {"b": 3}
s1 = {1, "x"}; s2 = {2, "y"}; fs = frozenset([1, 2])
D = [
 ("literal", lambda: {"a": 1}), ("a|b", lambda: a | b), ("b.__ror__", lambda: b.__ror__(a)),
 ("copy", lambda: a.copy()), ("fromkeys", lambda: dict.fromkeys("ab")), ("comp", lambda: {k: 1 for k in "ab"}),
 ("dict()", lambda: dict()), ("dict(a)", lambda: dict(a)), ("dict(kw)", lambda: dict(x=1)), ("dict(pairs)", lambda: dict([(1, 2)])),
 ("{**a}", lambda: {**a}), ("{**a,**b}", lambda: {**a, **b}), ("vars-copy", lambda: dict(vars(collections))),
 ("defaultdict|", lambda: collections.defaultdict(int, a) | b), ("defaultdict.copy", lambda: collections.defaultdict(int, a).copy()),
 ("OrderedDict|", lambda: collections.OrderedDict(a) | b), ("OrderedDict.copy", lambda: collections.OrderedDict(a).copy()),
 ("Counter+", lambda: collections.Counter("ab") + collections.Counter("b")), ("Counter.copy", lambda: collections.Counter("ab").copy()),
 ("kwargs", (lambda **k: k)), ("globals-copy", lambda: globals().copy()),
 ("ChainMap.new_child.maps0", lambda: collections.ChainMap(a).new_child().maps[0]),
]
S = [
 ("literal", lambda: {1}), ("set()", lambda: set()), ("set(fs)", lambda: set(fs)), ("set(list)", lambda: set([1])),
 ("s1|s2", lambda: s1 | s2), ("s1&s2", lambda: s1 & {1}), ("s1-s2", lambda: s1 - {1}), ("s1^s2", lambda: s1 ^ s2),
 ("union", lambda: s1.union(s2)), ("intersection", lambda: s1.intersection(s1)), ("difference", lambda: s1.difference(s2)),
 ("symdiff", lambda: s1.symmetric_difference(s2)), ("copy", lambda: s1.copy()), ("comp", lambda: {x for x in "ab"}),
 ("keys&", lambda: a.keys() & b.keys()), ("keys|", lambda: a.keys() | b.keys()), ("keys-", lambda: a.keys() - b.keys()), ("keys^", lambda: a.keys() ^ b.keys()),
 ("items|", lambda: a.items() | b.items()), ("set(keys)", lambda: set(a.keys())), ("set(fs|fs)", lambda: set(fs | fs)),
 ("fs.copy->set", lambda: set(fs.copy())), ("set(s1)", lambda: set(s1)), ("{*s1}", lambda: {*s1}), ("set(dict)", lambda: set(a)),
]
for n, m in D: chk("dict " + n, m, "d")
for n, m in S: chk("set " + n, m, "s")
assert not fails, fails
print("dict_set_results_mutable: ok")
