# Regression test: a starred assignment target binds in the enclosing scope.
#
# collectDefinedNames did not look inside a StarredNode, so in
# `first, *others = rest` the name `others` was not a local of the function
# and was stored with STORE_GLOBAL: it leaked into the module's globals.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


def split(rest):
    first, *others = rest
    return first, others


check("split", split([1, 2, 3]), (1, [2, 3]))
assert "others" not in globals(), "starred target leaked into the module globals"


def middle(seq):
    a, *mid, z = seq
    [p, *q] = mid
    for h, *t in [(1, 2, 3)]:
        pass
    return sorted(locals())


check("starred names are locals", middle([1, 2, 3, 4]), ["a", "h", "mid", "p", "q", "seq", "t", "z"])
for name in ("mid", "q", "t"):
    assert name not in globals(), "%r leaked into the module globals" % name
print("starred_target_scope: ok")
