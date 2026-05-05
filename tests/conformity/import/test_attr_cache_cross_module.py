# Regression test: cross-module attribute name corruption.
#
# Captures the failure mode that triggered protoCore commit 84974040
# ("revert attribute-cache rework chain"): when modules with many
# string attributes were imported in sequence, the new cache mishashed
# keys, so loading `importlib.machinery` and then `_weakref` produced
# a `_weakref` namespace where:
#   - The first three attribute names came back as empty strings.
#   - `_check_name` (an attribute of importlib.machinery) appeared in
#     `_weakref.__dict__` as a spurious entry.
#
# Any future re-attempt of the attribute-cache rework MUST keep this
# test green.  It runs in <1 s on a clean build.
#
# See `tasks/perf_investigation_plan.md` (path #2) for the broader
# context of what we are guarding here.

import importlib.machinery
import _weakref


def assert_eq(actual, expected, msg):
    if actual != expected:
        raise AssertionError(f"{msg}: expected {expected!r}, got {actual!r}")


def main():
    attrs = dir(_weakref)

    # Invariant 1: every attribute name is a non-empty string.  The
    # original symptom was that 3 names came back as "" after the bad
    # cache mishashed keys.
    empty_count = sum(1 for a in attrs if a == "")
    assert_eq(empty_count, 0,
              "_weakref has empty-string attribute names "
              f"(corruption symptom; full list = {attrs})")

    # Invariant 2: the canonical _weakref attributes are present.  Any
    # cache that drops or renames them fails this check.
    expected_present = ("ReferenceType", "CallableProxyType", "ProxyType",
                        "ref", "proxy", "getweakrefs", "getweakrefcount")
    for name in expected_present:
        if name not in attrs:
            raise AssertionError(
                f"_weakref.{name} missing from dir(); "
                f"attr-cache likely lost the entry. dir = {attrs}")

    # Invariant 3: importlib.machinery's private helpers MUST NOT leak
    # into _weakref's namespace.  `_check_name` is the canonical
    # canary — it is defined in importlib._bootstrap_external and was
    # the spurious attribute observed in the original bug report.
    leaked = [name for name in ("_check_name", "_classify_pyc",
                                 "_compile_source_to_code")
              if name in attrs]
    if leaked:
        raise AssertionError(
            f"importlib.machinery internals leaked into _weakref: "
            f"{leaked}; full _weakref dir = {attrs}")

    # Invariant 4: round-trip through getattr.  Even if dir() looks
    # right, the actual attribute resolution must succeed for each
    # advertised name.  A cache that returns wrong values for some
    # subset of keys would pass invariants 1-3 and still corrupt
    # downstream code — this catches that.
    for name in expected_present:
        val = getattr(_weakref, name, None)
        if val is None:
            raise AssertionError(
                f"getattr(_weakref, {name!r}) returned None; "
                f"cache returned a stale or missing value")

    print("test_attr_cache_cross_module passed")


main()
