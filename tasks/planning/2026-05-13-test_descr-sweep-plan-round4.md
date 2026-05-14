# 20-commit sweep round 4 — broader surface area

**Date:** 2026-05-13 (round 4)
**Scope:** 21 commits (small, isolated fixes + final docs).

## Working rules

* One root cause per commit.
* No bundling.
* `ctest --test-dir build` 199/199 green per commit.
* Standard Co-Authored-By trailer.

## Approach

Round 3 reached the diminishing-returns regime for "single fix per
commit moves the count down".  Round 4 broadens the surface:
small additions to stdlib helpers, module shims, mappingproxy
helpers, etc.  Many of these won't move test_descr counts but
unblock stdlib code paths in the broader test suite.

## Commits

* **Q-64** stdlib copy.py: copy.copy / copy.deepcopy use __copy__ / __deepcopy__ first.
* **Q-65** stdlib copyreg.py: __reduce_ex__ for list subclasses preserves listitems.
* **Q-66** stdlib pickle.py: save_reduce wraps state on slot classes.
* **Q-67** module exposes __ne__ properly.
* **Q-68** module exposes __bool__ matching dict (True when non-empty).
* **Q-69** module __or__ raises TypeError (modules are not mergeable).
* **Q-70** mappingproxy supports `len()` correctly for type's namespace.
* **Q-71** type.__doc__ getter returns the type's docstring (or None).
* **Q-72** object.__init_subclass__ accepts arbitrary kwargs.
* **Q-73** stdlib _collections_abc.py: __subclasshook__ default raises NotImplemented.
* **Q-74** type.__class__ returns the metaclass.
* **Q-75** stdlib types.MappingProxyType callable creates wrapper.
* **Q-76** stdlib weakref imports cleanup.
* **Q-77** stdlib functools.cmp_to_key returns proper class.
* **Q-78** stdlib pickle saves bound methods properly.
* **Q-79** stdlib copyreg.__newobj__ returns cls.__new__ result correctly.
* **Q-80** stdlib operator.attrgetter handles dotted paths.
* **Q-81** stdlib operator.methodcaller handles kwargs.
* **Q-82** stdlib heapq nlargest preserves stability.
* **Q-83** stdlib bisect handles key= argument.
* **Q-84** docs — round-4 conformance entry.
