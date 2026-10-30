# str.format_map(mapping) reads named fields from the mapping itself.
class Default(dict):
    def __missing__(self, key):
        return "<" + key + ">"


assert "a={a!r}, b={b}".format_map({"a": "x", "b": 2}) == "a='x', b=2"
assert "{name} is {age:>3}".format_map(Default(name="Ann")) == "Ann is <age>"
assert "{x.real}".format_map({"x": 5}) == "5"
try:
    "{missing}".format_map({})
except KeyError as e:
    assert e.args == ("missing",), e.args
else:
    raise AssertionError("no KeyError for a missing name")
try:
    "{a}".format_map()
except TypeError:
    pass
else:
    raise AssertionError("format_map() without a mapping accepted")
# A value's own formatting does not see the outer mapping.
class F:
    def __format__(self, spec):
        return "{a}".format(a="inner")
assert "{f}".format_map({"f": F(), "a": "outer"}) == "inner"
assert "{a}".format(a=1) == "1"
print("str_format_map: ok")
