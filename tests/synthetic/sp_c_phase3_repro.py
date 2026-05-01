"""SP-C / Phase 3 reproducer — all MappingProxy methods own-only.

Tests:
  1. keys()       — returns only own attribute names
  2. values()     — returns only own attribute values
  3. items()      — returns only own (key, value) pairs
  4. __iter__     — same set as keys()
  5. __getitem__  — KeyError for inherited names; native classes too
  6. __len__      — count of own attributes only
  7. get()        — returns default for inherited/missing names
"""
class P:
    x = 1
    y = "hello"

d = P.__dict__

# 1. keys()
keys = list(d.keys())
assert 'x' in keys and 'y' in keys, f"own attrs missing: {keys}"
assert '__init__' not in keys, f"inherited '__init__' should not be in keys: {keys}"

# 2. values()
vals = list(d.values())
assert 1 in vals and "hello" in vals, f"own values missing: {vals}"

# 3. items() — build a regular dict via explicit iteration to side-step
# pre-existing dict(iterable_of_tuples) limitations in protoPython.
items_list = list(d.items())
items = {}
for _pair in items_list:
    items[_pair[0]] = _pair[1]
assert items.get('x') == 1
assert items.get('y') == "hello"
assert '__init__' not in items, f"inherited in items: {list(items.keys())}"

# 4. __iter__
iter_keys = [k for k in d]
assert 'x' in iter_keys
assert '__init__' not in iter_keys, f"inherited in iter: {iter_keys}"

# 5. __getitem__
assert d['x'] == 1
try:
    _ = d['__init__']
    raise AssertionError("d['__init__'] should KeyError (inherited)")
except KeyError:
    pass

# Native class hardening — fences the C2 cascade root cause
try:
    _ = str.__dict__['__dataclass_fields__']
    raise AssertionError("str.__dict__['__dataclass_fields__'] should KeyError")
except KeyError:
    pass

# 6. __len__ matches __iter__ count
n_own = sum(1 for _ in d)
assert len(d) == n_own, f"len(d)={len(d)} != iter count {n_own}"

# 7. get()
assert d.get('x') == 1
assert d.get('__init__', 'sentinel') == 'sentinel', "get(inherited) should return default"
assert d.get('nonexistent') is None
# Native class hardening
assert str.__dict__.get('__dataclass_fields__', 'sentinel') == 'sentinel'

print("SP_C_PHASE3_OK")
