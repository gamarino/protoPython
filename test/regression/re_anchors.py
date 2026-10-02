# Anchors, line boundaries and `.` in `re` follow CPython on every platform.
#
# Without re.MULTILINE, `^` matches only at the start of the string and `$`
# only at the end or before a final newline; with it, both match at every "\n"
# (and only "\n": "\r", "\r\n" and U+2028 are not line boundaries in Python).
# `\A` and `\Z` are the start and the end of the string in every mode. `.`
# matches everything but "\n" unless re.DOTALL is given.
#
# The engine under `re` is the C++ standard library's ECMAScript std::regex,
# whose line handling differs from Python's and between implementations
# (MSVC's ^ and $ matched at every line without MULTILINE, so
# re.match(r'^[a-z]+$', 'abc\nevil') succeeded there). Every expected value
# below was produced by CPython 3.14.
import re

M, S = re.M, re.S

CASES = [
    (('match', '^[a-z]+$', 'abc\nevil', 0), None),
    (('match', '^[a-z]+$', 'abc\n', 0), ('M', (0, 3), 'abc', ())),
    (('match', '^[a-z]+$', 'abc', 0), ('M', (0, 3), 'abc', ())),
    (('search', 'abc$', 'abc\n', 0), ('M', (0, 3), 'abc', ())),
    (('search', 'abc$', 'abc\n\n', 0), None),
    (('search', 'abc\\Z', 'abc\n', 0), None),
    (('search', 'abc\\Z', 'abc', 0), ('M', (0, 3), 'abc', ())),
    (('search', '\\Aabc', 'x\nabc', 0), None),
    (('search', '\\Aabc', 'x\nabc', M), None),
    (('search', '^abc', 'x\nabc', 0), None),
    (('search', '^abc', 'x\nabc', M), ('M', (2, 5), 'abc', ())),
    (('search', '^abc', 'x\rabc', M), None),
    (('search', 'x$', 'x\r\n', M), None),
    (('search', 'x$', 'x\ry', M), None),
    (('search', 'abc\\Z', 'abc\n', M), None),
    (('findall', '^\\w+', 'ab\ncd\nef', 0), ['ab']),
    (('findall', '^\\w+', 'ab\ncd\nef', M), ['ab', 'cd', 'ef']),
    (('findall', '\\w+$', 'ab\ncd\nef\n', 0), ['ef']),
    (('findall', '\\w+$', 'ab\ncd\nef\n', M), ['ab', 'cd', 'ef']),
    (('findall', '(?m)^\\w+$', 'ab\ncd ef\ngh', 0), ['ab', 'gh']),
    (('findall', '^', 'a\nb\n', M), ['', '', '']),
    (('findall', '$', 'a\nb\n', M), ['', '', '']),
    (('findall', '$', 'a\nb\n', 0), ['', '']),
    (('findall', '.+', 'ab\ncd\r\nef', 0), ['ab', 'cd\r', 'ef']),
    (('findall', '.+', 'ab\ncd', S), ['ab\ncd']),
    (('findall', '(?s).+', 'ab\ncd', 0), ['ab\ncd']),
    (('findall', '[^a]+', 'b\nab\n', 0), ['b\n', 'b\n']),
    (('findall', '\\s+', 'a \n\r\t b\u2028c', 0), [' \n\r\t ', '\u2028']),
    (('findall', '\\S+', 'a \n\r\t b\u2028c', 0), ['a', 'b', 'c']),
    (('findall', '\\W+', 'a,\nb', 0), [',\n']),
    (('findall', '[\\n\\r]+', 'a\r\nb\nc', 0), ['\r\n', '\n']),
    (('findall', '[\\s]+', 'a\n b', 0), ['\n ']),
    (('findall', '[\\S]+', 'a\n b', 0), ['a', 'b']),
    (('findall', '[^\\n]+', 'ab\ncd', 0), ['ab', 'cd']),
    (('findall', '[\\x00-\\x7f]+', 'ab\ncdé', 0), ['ab\ncd']),
    (('findall', 'a\\nb', 'a\nb a\rb', 0), ['a\nb']),
    (('findall', '^a\\n^b', 'a\nb', M), ['a\nb']),
    (('findall', '\\n^', '\n\n', M), ['\n', '\n']),
    (('findall', '^\\s*#.*$', 'x = 1\n  # c1\n#c2\ny', M), ['  # c1', '#c2']),
    (('findall', '(^[ \\t]*)(?:[^ \\t\\n])', '  a\n\tb\n c', M), ['  ', '\t', ' ']),
    (('findall', '^[ \\t]+$', 'a\n  \n\t\nb', M), ['  ', '\t']),
    (('sub', '^', '> ', 'a\nb\n', M), '> a\n> b\n> '),
    (('sub', '$', '<', 'a\nb\n', M), 'a<\nb<\n<'),
    (('sub', '$', '<', 'a\nb\n', 0), 'a\nb<\n<'),
    (('sub', '^\\s+', '', '  a\n  b', M), 'a\nb'),
    (('sub', '\\s+$', '', 'a  \nb  \n', M), 'a\nb'),
    (('split', '\\n', 'a\nb\nc', 0), ['a', 'b', 'c']),
    (('split', '$', 'a\nb', M), ['a', '\nb', '']),
    (('fullmatch', 'abc$', 'abc', 0), ('M', (0, 3), 'abc', ())),
    (('fullmatch', 'abc$', 'abc\n', 0), None),
    (('fullmatch', 'abc\\n', 'abc\n', 0), ('M', (0, 4), 'abc\n', ())),
    (('fullmatch', '(?m)a\\n^b$', 'a\nb', 0), ('M', (0, 3), 'a\nb', ())),
    (('search', '\\bb', 'a\nb', M), ('M', (2, 3), 'b', ())),
    (('search', '\\n\\B', '\nb', M), None),
    (('search', '\\n\\B', '\n', M), ('M', (0, 1), '\n', ())),
    (('search', '\\n\\b', '\nb', M), ('M', (0, 1), '\n', ())),
    (('findall', '(?i)^AB', 'ab\nAb', M), ['ab', 'Ab']),
    (('findall', '(a|^b)', 'xb\nb', M), ['b']),
    (('findall', '(?m)(\\w)$', 'ab\ncd', 0), ['b', 'd']),
    (('search', 'x.y', 'x\ny', 0), None),
    (('search', 'x.y', 'x\ry', 0), ('M', (0, 3), 'x\ry', ())),
    (('search', 'x.y', 'x\u2028y', 0), ('M', (0, 3), 'x\u2028y', ())),
    (('search', 'x.y', 'x\ny', S), ('M', (0, 3), 'x\ny', ())),
    (('match', '(?m)a$', 'a\nb', 0), ('M', (0, 1), 'a', ())),
    (('match', 'a$', 'a\nb', 0), None),
    (('P', 'match', '^a', 'ba', 0, 1, None), None),
    (('P', 'search', '^a', 'ba', 0, 1, None), None),
    (('P', 'search', '^a', 'b\na', M, 1, None), ('M', (2, 3), 'a', ())),
    (('P', 'search', '^a', 'b\na', M, 2, None), ('M', (2, 3), 'a', ())),
    (('P', 'search', 'a$', 'ab', 0, 0, 1), ('M', (0, 1), 'a', ())),
    (('P', 'search', 'a\\Z', 'ab', 0, 0, 1), ('M', (0, 1), 'a', ())),
    (('P', 'search', '\\Ab', 'ab', 0, 1, None), None),
    (('P', 'findall', '^', 'a\nb', M, 0, 2), ['', '']),
]


def norm(r):
    if r is None:
        return None
    if isinstance(r, (list, str)):
        return r
    return ("M", r.span(), r.group(0), r.groups())


def run(case):
    if case[0] == "P":
        _, func, pattern, string, flags, pos, endpos = case
        compiled = re.compile(pattern, flags)
        args = (string, pos) if endpos is None else (string, pos, endpos)
        return norm(getattr(compiled, func)(*args))
    if case[0] == "sub":
        _, pattern, repl, string, flags = case
        return re.sub(pattern, repl, string, flags=flags)
    if case[0] == "split":
        _, pattern, string, flags = case
        return re.split(pattern, string, flags=flags)
    func, pattern, string, flags = case
    got = norm(getattr(re, func)(pattern, string, flags))
    # The compiled pattern agrees with the module-level function.
    again = norm(getattr(re.compile(pattern, flags), func)(string))
    if again != got:
        return ("compiled pattern disagrees", again, got)
    return got


failures = []
for case, expected in CASES:
    try:
        got = run(case)
    except Exception as e:
        got = ("raised", type(e).__name__, str(e))
    if got != expected:
        failures.append((case, got, expected))

for f in failures:
    print("MISMATCH", f)
assert not failures, "%d of %d re anchor cases differ from CPython" % (len(failures), len(CASES))
print("re_anchors: ok (%d cases)" % len(CASES))
