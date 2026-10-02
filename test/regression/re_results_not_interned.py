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

# Memory bound: many distinct results must not accumulate. The test is run
# under PROTOCORE_HEAP_LIMIT_CELLS (see CMakeLists.txt) so the collector
# actually runs; interned results could never be reclaimed at all. This part
# runs first so that, if results are interned again, it is the bound that
# fails (the identity checks below would catch that too).
#
# sys._current_rss() is protoPython's resident set size of the process in
# bytes (/proc on Linux, task_info on macOS, GetProcessMemoryInfo on Windows),
# or -1 where it cannot be read. The token count is checked everywhere; only
# the memory bound depends on it, and a platform without it says so.
#
# What is bounded is growth after a warm-up, measured in this same process,
# not the absolute RSS increase from the start: the first rounds fill the
# heap up to the cell limit and grow the allocator's arenas, and that cost
# depends on the platform allocator (it was 29.1 MB on Linux and 35.7 MB on
# macOS for the same loop). Once the heap has reached its working size,
# collectable results are reclaimed and RSS stays flat, while interned ones
# keep adding to it at the same rate as before.
#
# The subjects are built with "+" and str(), which give collectable strings;
# "%"-formatted strings are still interned and would grow RSS on their own
# (see PythonEnvironment::getInternedString in PythonEnvironment.h).
def rss_kb():
    rss = sys._current_rss()
    return rss // 1024 if rss >= 0 else -1


WORD = re.compile(r"[a-z0-9_]+")


def tokenize_round(tag, count):
    """findall over `count` distinct subjects; returns the number of tokens."""
    n = 0
    for i in range(count):
        text = tag + "_" + str(i) + "_value_" + str(i * 7) + " end"
        n += len(WORD.findall(text))
    return n


ROUND = 20000
# Two tokens per subject: "<tag>_<i>_value_<j>" and "end".
assert tokenize_round("warmup", ROUND) == 2 * ROUND
baseline = rss_kb()
for r in range(2):
    assert tokenize_round("round%d" % r, ROUND) == 2 * ROUND
if baseline > 0:
    growth = rss_kb() - baseline
    # Measured on Linux under PROTOCORE_HEAP_LIMIT_CELLS=500000, over the
    # 40000 calls after the warm-up: within +-1.5 MB with collectable results
    # (a collection cycle landing on either side of a measurement), about
    # 22 MB with results interned again. The bound sits well between the two.
    assert growth < 8000, (
        "RSS grew by %d KB over %d re calls after warm-up" % (growth, 2 * ROUND))
    print("re_results_not_interned: RSS grew by %d KB after warm-up" % growth)
else:
    print("re_results_not_interned: SKIPPED the RSS bound: sys._current_rss() "
          "is not available on " + sys.platform)

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


print("re_results_not_interned: ok")
