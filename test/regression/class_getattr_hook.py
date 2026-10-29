# A class's __getattr__ serves its instances; a missing attribute of the
# class itself falls back to the metaclass's __getattr__ (CPython).  Reading
# a missing attribute of such a class used to call the class's own
# __getattr__ with only the name: unittest's
# getattr(_FailedTest, "__unittest_skip__", False) raised TypeError and the
# whole run of a test file with an import error died.
import types


class C:
    def __getattr__(self, name):
        return "inst:" + name


assert C().x == "inst:x"
assert getattr(C, "x", "dflt") == "dflt"
assert hasattr(C, "x") is False
try:
    C.x
except AttributeError:
    pass
else:
    raise AssertionError("C.x did not raise AttributeError")


class Meta(type):
    def __getattr__(cls, name):
        return "meta:" + name


class D(metaclass=Meta):
    def __getattr__(self, name):
        return "inst:" + name


assert D.y == "meta:y" and getattr(D, "y") == "meta:y" and hasattr(D, "y")
assert D().y == "inst:y"

# A module's own __getattr__(name) still takes only the name.
m = types.ModuleType("m")
m.__getattr__ = lambda name: "mod:" + name
assert m.z == "mod:z" and getattr(m, "z") == "mod:z"

import unittest
loader = unittest.TestLoader()
suite = loader.loadTestsFromName("no_such_module_for_class_getattr_hook")
result = unittest.TestResult()
suite.run(result)
assert result.testsRun == 1 and len(result.errors) == 1, (result.testsRun, result.errors)
print("class_getattr_hook: ok")
