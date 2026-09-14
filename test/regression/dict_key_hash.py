# Regression test: dict keys are found through protoCore's hash alone.
#
# __getitem__, __setitem__ and __delitem__ used to fall back to scanning
# every stored key with __eq__ whenever the hash bucket missed. The scan
# worked around an identity-based ProtoTuple hash that protoCore has since
# replaced with a content-based one (STRUCT-194), but it ran on every
# insertion of a new key, which made building a dict quadratic (2000 keys
# took over 4 seconds). Equal keys must hash equally instead, so a bucket
# miss means the key is absent.
import time


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


# Numbers that compare equal are the same key.
d = {1: "int"}
check("d[1.0]", d[1.0], "int")
check("d[True]", d[True], "int")
check("1.0 in d", 1.0 in d, True)
d[1.0] = "float"
d[True] = "bool"
check("1 / 1.0 / True share one entry", (len(d), d[1]), (1, "bool"))
del d[1.0]
check("del by equal float", len(d), 0)

# Strings built at runtime are the same key as literals, short or long.
lit = {"alpha": 1, "a_rather_long_key_that_is_not_inline_0123456789": 2, "x" * 300: 3}
part = "al"
check("short concat", lit[part + "pha"], 1)
check("short decode", lit[b"alpha".decode()], 1)
longp = "a_rather_long_key_that_is_not_inline_"
check("long concat", lit[longp + "0123456789"], 2)
check("long decode", lit["a_rather_long_key_that_is_not_inline_0123456789".encode().decode()], 2)
check("long join", lit["".join(["a_rather_long_key_that_is_", "not_inline_0123456789"])], 2)
check("huge join", lit["".join(["x" * 150, "x" * 150])], 3)
check("huge concat", lit["x" + "x" * 299], 3)
lit[longp + "0123456789"] = 20
check("long concat setitem updates in place", (len(lit), lit["a_rather_long_key_that_is_not_inline_0123456789"]), (3, 20))

# Tuple keys over decoded strings (pickle's _compat_pickle lookups).
names = {("copy_reg", "_reconstructor"): "x"}
check("tuple decoded", names[(b"copy_reg".decode(), b"_reconstructor".decode())], "x")
names[(b"copy_reg".decode(), "_reconstructor")] = "y"
check("tuple setitem updates in place", (len(names), names[("copy_reg", "_reconstructor")]), (1, "y"))


# User types with a content __hash__ / __eq__.
class P:
    def __init__(self, x):
        self.x = x

    def __hash__(self):
        return hash(self.x)

    def __eq__(self, other):
        return isinstance(other, P) and other.x == self.x


u = {P(3): "p"}
check("user key lookup", u[P(3)], "p")
u[P(3)] = "q"
check("user key setitem", (len(u), u[P(3)]), (1, "q"))
del u[P(3)]
check("user key delitem", len(u), 0)


class cistr(str):
    def __hash__(self):
        return hash(self.lower())

    def __eq__(self, other):
        return self.lower() == str(other).lower()


c = {cistr("TWO"): 2}
check("str subclass hash", c[cistr("two")], 2)

# Missing keys still raise KeyError.
try:
    lit["absent"]
except KeyError:
    pass
else:
    raise AssertionError("missing key did not raise KeyError")

# Inserting new keys is linear: with the scan 20000 keys took minutes.
N = 20000
keys = ["key%d" % i for i in range(N)]
start = time.time()
big = {}
for i in range(N):
    big[keys[i]] = i
elapsed = time.time() - start
check("big dict size", len(big), N)
check("big dict lookup", big["key%d" % (N - 1)], N - 1)
assert elapsed < 20.0, "inserting %d keys took %.1fs (quadratic?)" % (N, elapsed)

print("dict key hash OK")
