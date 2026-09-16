# Strings computed by `re` are ordinary collectable objects, not interned
# symbols.
#
# Every match, group, sub, split, findall and escape result used to go through
# PythonEnvironment::getInternedString, which returns a ProtoString symbol that
# protoCore never collects and also records it in a process-wide pool. Each
# distinct result therefore stayed in memory for the life of the process, and
# `re` over varying input grew without bound.
#
# Identity is the observable consequence: two equal results must be distinct
# objects. Strings of up to 6 UTF-8 bytes are stored inside the pointer by
# protoCore, so for those `is` is value equality and proves nothing; every
# string used here is longer.

import re
import sys

LONG = "token_alpha_0123456789"
assert len(LONG.encode()) > 6

PAT = re.compile(r"[a-z]+_[a-z]+_[0-9]+")

a = PAT.findall(LONG + " tail")[0]
b = PAT.findall(LONG + " tail")[0]
assert a == b == LONG
assert a is not b, "equal findall results are the same interned object"

m1 = PAT.search(LONG + " tail")
m2 = PAT.search(LONG + " tail")
assert m1.group(0) == m2.group(0) == LONG
assert m1.group(0) is not m2.group(0), "equal match results share one object"

# Groups, sub and split results likewise.
GPAT = re.compile(r"([a-z]+_[a-z]+)_([0-9]+)")
g1 = GPAT.search(LONG).group(1)
g2 = GPAT.search(LONG).group(1)
assert g1 == g2 == "token_alpha"
assert g1 is not g2, "equal group results share one object"

s1 = re.sub(r"[0-9]+", "replacement_text", "value 1")
s2 = re.sub(r"[0-9]+", "replacement_text", "value 1")
assert s1 == s2 == "value replacement_text"
assert s1 is not s2, "equal sub results share one object"

p1 = re.split(r",", "first_element_here,second")[0]
p2 = re.split(r",", "first_element_here,second")[0]
assert p1 == p2 == "first_element_here"
assert p1 is not p2, "equal split results share one object"

e1 = re.escape("a_long_escaped_value+")
e2 = re.escape("a_long_escaped_value+")
assert e1 == e2
assert e1 is not e2, "equal escape results share one object"

# The pattern source a compiled pattern remembers is not interned either:
# re.compile stored it as a symbol, so every distinct pattern text was
# permanent.
P1 = re.compile(r"[a-z]+_[a-z]+_[0-9]+")
P2 = re.compile(r"[a-z]+_[a-z]+_[0-9]+")
assert P1.pattern == P2.pattern == r"[a-z]+_[a-z]+_[0-9]+"
assert P1.pattern is not P2.pattern, "the pattern source is interned"

# Values are still correct after all of this.
assert PAT.findall("one_two_11 three_four_22") == ["one_two_11", "three_four_22"]
assert re.sub(r"\d+", "#", "a1b22c333") == "a#b#c#"
assert re.split(r"\s+", "x  y   z") == ["x", "y", "z"]
assert re.match(r"(\w+)@(\w+)", "user@host").groups() == ("user", "host")
assert re.escape("a.b") == "a\\.b"

# Vocabulary stays interned: attribute names reached through getattr still
# resolve, which is what interning is for.
m = GPAT.search(LONG)
assert getattr(m, "group")(2) == "0123456789"


# Memory bound: many distinct results must not accumulate. The test is run
# under PROTOCORE_HEAP_LIMIT_CELLS (see CMakeLists.txt) so the collector
# actually runs; interned results could never be reclaimed at all.
def rss_kb():
    f = open("/proc/self/status")
    data = f.read()
    f.close()
    for line in data.split("\n"):
        if line.startswith("VmRSS"):
            return int(line.split()[1])
    return -1


start = rss_kb()
if start > 0:
    WORD = re.compile(r"[a-z0-9_]+")
    n = 0
    for i in range(20000):
        text = "item_%d_value_%d end" % (i, i * 7)
        n += len(WORD.findall(text))
    growth = rss_kb() - start
    # Two tokens per line: "item_<i>_value_<j>" and "end".
    assert n == 40000, n
    # Measured under PROTOCORE_HEAP_LIMIT_CELLS=500000, three runs each and a
    # spread under 0.2 MB: this loop grew RSS by about 42.1 MB with interned
    # results and about 29.1 MB with collectable ones. The bound sits between
    # the two, with headroom over the current figure so the test is not a
    # flake. What remains is mostly the "%"-formatted subjects, which are still
    # interned; see protoPython_interned_data_strings_options.md.
    assert growth < 35000, "RSS grew by %d KB over 20000 re calls" % growth

print("re_results_not_interned: ok")
