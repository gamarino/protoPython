# A 100 KB str costs a few thousand protoCore cells (36-byte rope leaves plus
# internal nodes) however it is built, and the common operations on it do not
# hold a cell, or a few dozen cells, per character until they return.
#
# CMakeLists.txt runs this file under a fixed heap limit of 2,000,000 cells
# (PROTOCORE_ADAPTIVE_HEAP=0). Measured by heap-limit bisection on Linux, it
# needs about 1.25 M cells now and needed about 12.6 M before: iterating a
# 100 KB str exploded it into a ProtoList of its characters (about 1.6 M
# cells in flight), reversed() and extended slices built lists of
# one-character strings (up to 5 M), and a native consumer of a generator
# (str.join, sum) kept about 16 cells per step alive until it returned.
import re

N = 100_000
text = "word " * (N // 5)
assert len(text) == N

# Construction paths.
built = [
    "a" * N,
    "".join(["word "] * (N // 5)),
    "".join(["a" for _ in range(N)]),
    "".join("a" for _ in range(N)),
    (text * 2)[:N],
    text.upper(),
    text.replace("w", "W"),
    "%s" % text,
    f"{text}",
    text.encode().decode(),
]
s = ""
for _ in range(N):
    s += "a"
built.append(s)
for b in built:
    assert len(b) == N, len(b)

# Iteration and the consumers built on it.
count = 0
for ch in text:
    count += 1
assert count == N
assert sum(1 for _ in text) == N
assert sum(map(ord, text)) == sum(ord(c) for c in "word ") * (N // 5)
assert len(set(text)) == 5
assert sum(1 for _ in enumerate(text)) == N

# Slices with a step and reversal.
assert text[::-1][:5] == " drow" and len(text[::-1]) == N
assert len(text[::2]) == N // 2
assert "".join(reversed(text))[:5] == " drow"

# str.join over a str, a generator and a list.
assert len(",".join(text)) == 2 * N - 1
assert len("".join(c for c in text)) == N
assert len("".join(list(text[:1000]))) == 1000

# The re module on the same subject.
tok = re.compile(r"\w+|\s+")
pos = tokens = 0
while pos < N:
    pos = tok.match(text, pos).end()
    tokens += 1
assert tokens == 2 * (N // 5)
assert len(re.findall(r"\w+", text)) == N // 5

# Churn: the collector must run while the strings above are alive.
garbage = None
for _ in range(200):
    garbage = [(i,) for i in range(1000)]

print("str_cells_bounded: ok")
