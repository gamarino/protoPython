# str.format with a field that has no argument raises, as in CPython:
# KeyError for a named field, IndexError for a positional one.  protoPython
# formatted the missing value as 'None'.
def raises(exc, f):
    try:
        f()
    except exc as e:
        return e
    raise AssertionError("%s not raised" % exc.__name__)

e = raises(KeyError, lambda: "{missing}".format())
assert e.args == ("missing",), e.args
raises(KeyError, lambda: "{a}{b}".format(a=1))
e = raises(IndexError, lambda: "{0}".format())
assert str(e) == "Replacement index 0 out of range for positional args tuple", str(e)
e = raises(IndexError, lambda: "{}{}".format(1))
assert str(e) == "Replacement index 1 out of range for positional args tuple", str(e)
e = raises(IndexError, lambda: str.format("{2}", 1, 2))
assert str(e) == "Replacement index 2 out of range for positional args tuple", str(e)
# Present arguments, including None, still format.
assert "{} {x}".format(None, x=0) == "None 0"
assert "{x}".format(x=None) == "None"
assert "{0} {0}".format(None) == "None None"
assert "{a.real}".format(a=3) == "3"
assert "{0[1]}".format([5, 6]) == "6"
assert "{k}".format_map({"k": 1}) == "1"
raises(KeyError, lambda: "{k}".format_map({}))
# Mixing automatic and manual numbering is a ValueError.
raises(ValueError, lambda: "{} {0}".format(1))
raises(ValueError, lambda: "{0} {}".format(1))
print("format_missing_field: ok")
