# A bound builtin method stored as a class attribute keeps its receiver when
# read through an instance: builtin bound methods are not descriptors.
# doctest.DocTestParser keeps `_IS_BLANK_OR_COMMENT = re.compile(...).match`
# and parsing any docstring failed with "descriptor requires a 're.Pattern'
# object", which stopped test_collections and test_generators at load.
import re

pat = re.compile(r"\s*$")


class P:
    blank = pat.match
    upper = "abc".upper
    length = len

    def check(self):
        return self.blank("   ") is not None


p = P()
assert p.check()
assert p.blank.__self__ is pat
assert p.upper() == "ABC"
assert p.length("ab") == 2
assert P.blank("x") is None
print("bound_builtin_class_attr: ok")
