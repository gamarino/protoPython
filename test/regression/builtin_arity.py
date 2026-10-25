# Regression test: built-ins called with too few arguments raise TypeError.
#
# py_len returned None when called without an argument (`len()`), as did
# several other built-ins whose native code returned PROTO_NONE on a short
# argument list instead of raising.


def raises_type_error(label, fn):
    try:
        fn()
    except TypeError:
        return
    except Exception as e:  # any other exception is a different bug
        raise AssertionError("%s: raised %r instead of TypeError" % (label, e))
    raise AssertionError("%s: no TypeError" % label)


raises_type_error("len()", lambda: len())
for name in ("next", "repr", "format", "enumerate", "sum", "compile", "eval", "exec", "sorted",
             "hash", "delattr", "abs", "divmod", "ascii", "bin", "oct", "hex", "map",
             "classmethod", "staticmethod", "isinstance", "issubclass", "callable", "id",
             "any", "all", "open", "range"):
    fn = getattr(__builtins__, name) if hasattr(__builtins__, name) else __builtins__[name]
    raises_type_error(name + "()", fn)

raises_type_error("divmod(1)", lambda: divmod(1))
raises_type_error("isinstance(1)", lambda: isinstance(1))
raises_type_error("map(len)", lambda: map(len))
raises_type_error("range(1, 2, 3, 4)", lambda: range(1, 2, 3, 4))
raises_type_error("range(3).__getitem__()", lambda: range(3).__getitem__())
raises_type_error("property().getter()", lambda: property().getter())

try:
    len()
except TypeError as e:
    assert str(e) == "len() takes exactly one argument (0 given)", str(e)

# The checks do not reject valid calls.
assert len([1]) == 1 and list(enumerate("ab")) == [(0, "a"), (1, "b")]
assert list(map(abs, [-1])) == [1] and divmod(7, 2) == (3, 1)
assert classmethod(len).__func__ is len and staticmethod(len).__func__ is len
assert list(range(3)) == [0, 1, 2] and any([1]) and all([])
print("builtin_arity: ok")
