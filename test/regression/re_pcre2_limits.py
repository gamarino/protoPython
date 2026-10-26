# What this re module (PCRE2 underneath) does not do as CPython does, and
# how it says so: every difference is an exception, never a silently wrong
# result. See docs/DESIGN_DECISIONS.md, "The re module runs on PCRE2".

import re


def raises(exc, fn, needle):
    try:
        fn()
    except exc as e:
        assert needle in str(e), "%s: %r lacks %r" % (exc.__name__, str(e), needle)
        return
    raise AssertionError("%s not raised (expected %r)" % (exc.__name__, needle))


# Backtracking beyond the engine's limits is a RecursionError. CPython takes
# exponential time on these; PCRE2 gives up after its match limit.
raises(RecursionError, lambda: re.search(r"(a|aa)*$", "a" * 60 + "b"), "backtracking")
raises(RecursionError, lambda: re.search(r"(?:a*)*$", "a" * 40 + "b"), "backtracking")

# PCRE2 caps repeat counts at 65535 (CPython: 4294967294).
raises(re.error, lambda: re.compile(r"a{65536}"), "65535")
assert re.match(r"a{65535}", "a" * 65535)

# Named Unicode escapes need the Unicode name database.
raises(re.error, lambda: re.compile(r"\N{DIGIT ONE}"), "not supported")

# PCRE2 cannot match a lone surrogate, so a subject holding one is refused
# rather than searched with the surrogate skipped.
raises(ValueError, lambda: re.search("a", "x\ud800a"), "lone surrogates")

# A surrogate in a pattern can match nothing then: it compiles to an empty
# class, and the rest of the pattern still works.
assert re.search("[a\ud800]", "xa").group() == "a"
assert re.search("\ud800|b", "ab").group() == "b"

# Case-insensitive back references under re.ASCII (PCRE2 would fold
# non-ASCII letters too).
raises(re.error, lambda: re.compile(r"(a)\1", re.I | re.A), "not supported")
assert re.match(r"(a)\1", "aa", re.I)
assert re.match(r"(a)\1", "aa", re.A) and not re.match(r"(a)\1", "aA", re.A)

# Long subjects are converted to UTF-8 once per thread and reused, so a
# tokenizer loop of pattern.match(text, pos) over one text stays linear.
def tokenize(text):
    tok = re.compile(r"\w+|\s+")
    pos = count = 0
    size = len(text)
    while pos < size:
        m = tok.match(text, pos)
        pos = m.end()
        count += 1
    return count


# 100 KB: above the cache threshold and the size at which matching runs
# outside the managed region.
assert tokenize("word " * 20000) == 40000

print("re_pcre2_limits: ok")
