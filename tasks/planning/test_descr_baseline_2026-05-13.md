# `test_descr.py` baseline — 2026-05-13 (post A-01)

**Binary:** `build/src/runtime/protopy` after the loop-head safepoint
and `module.__dict__` fixes (A-01).

**Headline:**

```
Ran 165 tests in 47.426 s
FAILED (failures=51, errors=21, skipped=10)
```

**Deltas vs the 2026-05-07 baseline** (per
`docs/CPYTHON_CONFORMANCE.md`):

| | run | fail | error | Δ vs 2026-05-07 |
| :--- | ---: | ---: | ---: | ---: |
| 2026-05-07 | 159 | 93 | 34 | (reference) |
| 2026-05-13 | 165 | **51** | **21** | **−42 fail, −13 error** |

Both numbers come from the same `unittest` runner; the
2026-05-07 baseline ran fewer tests because the import-time
blocker handled in A-01 collapsed earlier in the chain.

## Failures + errors by test class

```
59  ClassPropertiesAndMethods   (MRO, slots, descriptors, properties)
 5  PicklingTests                (__reduce__ / __reduce_ex__ chain)
 4  MroTest                      (__mro_entries__, conflict messages)
 2  TestGenericDescriptors       (PEP 695 generic descriptors)
 1  OperatorsTest                (test_complexes)
 1  MiscTests                    (test_type_lookup_mro_reference)
─────
72  total
```

`ClassPropertiesAndMethods` dominates (82 % of the failures).
That single class spans MRO walks, `__slots__`, descriptor
protocol, metaclass interaction, multiple inheritance, property
machinery, classmethod / staticmethod and `__getattr__` /
`__setattr__` / `__delattr__` hooks — i.e. it covers Phase C of
the plan (commits C-08 … C-12).

## Top exception types in the raw output

```
51  AssertionError       (assert*)
 7  TypeError
 6  AttributeError
 3  KeyError
 2  NameError
 1  RuntimeError
```

The 51 AssertionErrors map 1:1 to the 51 FAIL rows.  The
21 errors break down into AttributeError (6), TypeError (7),
KeyError (3), NameError (2), RuntimeError (1) and 2 others.

## Spot-check error patterns

Sample errors from the raw output (first 7 entries) — these
inform Phase C / D scope:

* `ERROR test_builtin_bases` — `AttributeError: 'module' object
  has no attribute 'values'`.  Iterating module attributes via
  `.values()`; the type that defines `.values()` isn't being
  routed via the same dict-view path as a regular dict.
* `ERROR test_classic_comparisons` — `NameError: name 'c' is
  not defined`.  A scope / closure issue inside a test fixture.
* `ERROR test_descrdoc` — `AttributeError: 'object' object has
  no attribute 'closed'`.  Descriptor docstring lookup.
* `ERROR test_getattr_hooks` — descriptor protocol corner case.
* `ERROR test_metaclass` — metaclass conflict error path.
* `ERROR test_mutable_bases` — `__bases__` reassignment.
* `ERROR test_pickle_slots` — `__getstate__`/`__setstate__`
  default contract.

## Subset that the plan's phases B–E should reach

| Plan phase | Tests in scope | Expected impact |
| :--- | :--- | :--- |
| B (OperatorsTest cluster) | `test_complexes`, `test_explicit_reverse_methods` | 4–5 rows |
| C (descriptor protocol)   | `test_slots*`, `test_classmethods`, `test_staticmethods`, `test_metaclass`, `test_dict_dunder`, `test_descrdoc`, `test_getattr_hooks` | 12–15 rows |
| D (pickling)              | `PicklingTests.*` | 5 rows |
| E (MRO / metaclass)       | `MroTest.*`, `test_metaclass`, `test_mro_disagreement` | 4–5 rows |
| F (`__annotate__` / UnionType) | indirect — these are test_grammar / test_types not test_descr | 0 here |

**Total addressable by this 20-commit block (rough upper bound):**
~30 of the 72 rows.  The rest are deeper / cross-cutting issues
that will need a follow-up block.

## How to reproduce

```bash
cd /home/gamarino/Documentos/proyectos/protoPython
timeout 180 build/src/runtime/protopy --path . --script test/cpython/test_descr.py \
    > /tmp/test_descr_baseline.txt 2>&1
tail -3 /tmp/test_descr_baseline.txt
grep -E "^(FAIL|ERROR):" /tmp/test_descr_baseline.txt | grep -oE "builtins\.[A-Z][a-zA-Z]+" \
    | sort | uniq -c | sort -rn
```

## Next session should pick from

* **B-04** — binaryOp `__rop__` fallback.  Touches
  `OperatorsTest.test_complexes` and a couple of
  `ClassPropertiesAndMethods.test_*_operator_override` rows.
* **C-08** — `__getattribute__` data-vs-non-data descriptor
  ordering.  Touches a wide swath of
  ClassPropertiesAndMethods (10+ tests; expect highest yield).
* **C-12** — `__slots__` AttributeError message format
  (`test_slots`, `test_slots_special*`, `test_slots_descriptor`
  etc.).  ~6 rows.
