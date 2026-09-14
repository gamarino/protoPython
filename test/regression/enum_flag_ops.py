"""enum.Flag / IntFlag bitwise operations build composite members."""
import enum
import re

# the FlagBoundary aliases are real members, not the None placeholders
assert enum.STRICT is enum.FlagBoundary.STRICT
assert enum.KEEP is enum.FlagBoundary.KEEP
assert enum.CONFORM is enum.FlagBoundary.CONFORM
assert enum.EJECT is enum.FlagBoundary.EJECT
assert enum.Flag._boundary_ is enum.STRICT
assert enum.IntFlag._boundary_ is enum.KEEP


class F(enum.IntFlag):
    X = 1
    Y = 2


assert F._boundary_ is enum.KEEP
assert F._flag_mask_ == 3 and F._all_bits_ == 3

xy = F.X | F.Y
assert type(xy) is F and xy == 3 and int(xy) == 3
assert F.X in xy and F.Y in xy and len(xy) == 2
assert list(xy) == [F.X, F.Y]
assert F(3) is xy
assert (F.X ^ F.Y) is xy
none = F.X & F.Y
assert type(none) is F and none == 0 and not none
assert (xy & F.Y) is F.Y
assert (~F.X) is F.Y and ~xy == 0
assert (F.X | 1) is F.X
extra = F.X | 4                     # KEEP boundary keeps unknown bits
assert type(extra) is F and extra == 5


class G(enum.Flag):
    R = 1
    W = 2
    E = 4


rw = G.R | G.W
assert type(rw) is G and rw.value == 3
assert G.R in rw and G.E not in rw
assert not (G.R & G.W) and (G.R & G.W).value == 0
assert ~G.R == G.W | G.E and (~G.R).value == 6
assert G(7) is (G.R | G.W | G.E)
try:
    G(8)                            # STRICT boundary rejects unknown bits
except ValueError:
    pass
else:
    raise AssertionError("G(8) did not raise")

flags = re.IGNORECASE | re.MULTILINE
assert isinstance(flags, int) and flags == 10
assert re.compile("^a", flags).findall("x\nA") == ["A"]

print("enum flag ops OK")
