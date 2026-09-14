"""str(KeyError(k)) is repr(k): a missing key reads as it was written."""
try:
    {}['a']
except KeyError as e:
    assert str(e) == "'a'", str(e)
    assert repr(e) == "KeyError('a')", repr(e)
else:
    raise AssertionError("no KeyError")

assert str(KeyError('')) == "''" and str(KeyError(1)) == "1"
assert str(KeyError()) == "" and str(KeyError(1, 2)) == "(1, 2)"


class MyKeyError(KeyError):
    pass


assert str(MyKeyError('k')) == "'k'"
assert str(LookupError('k')) == "k" and str(ValueError('k')) == "k"
print("keyerror str OK")
