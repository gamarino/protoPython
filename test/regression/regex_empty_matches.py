# re: patterns that can match the empty string, character classes with a
# literal '-', group spans and the Match repr.  regex_empty_matches.expected is
# CPython's output.
import re

print(re.match("A?", "AA"), re.search("A?", "AA"), re.fullmatch("A*", "AA"))
print(re.findall("A?", "AA"), re.findall("x*", "abc"), re.sub("x*", "-", "abc"))
print(re.split("x*", "axbc"), [m.span() for m in re.finditer("b*", "abba")])
print(re.match("[A-]*", "A-A"), re.match("[-AO]*", "AOA").group())
print(re.match("(-*A?-*)", "AA").group(), re.match("(-*[A-]*)", "A-A").groups())
print(re.findall("(?m)^x?", "x\nx\ny"), re.search("(?m)^a*", "b\naa").group())
p = re.compile("A*")
print(p.match("BAA", 1), p.search("BAA", 1), p.findall("BAA", 1))
print(re.sub("(?m)^", ">", "a\nb"), re.findall(r"\w*", "ab cd"))
m = re.match(r"(\w+) (?P<second>\w+)?(x)?", "hello world")
print(m.span(), m.span(1), m.start(2), m.end("second"), m.span(3), m.start(), m.end())
try:
    m.span(9)
except IndexError as e:
    print("IndexError", e)
print(repr(re.search("b+", "abbc")), repr(re.match("", "x")))
print(repr(re.compile("a+")), repr(re.compile("a", re.I | re.M)), type(re.compile("a")).__name__,
      type(re.match("a", "a")).__name__, isinstance(re.match("a", "a"), re.Match))
m = re.match(r"(?P<first>\w+) (?P<second>\w+)?", "hello world")
print(m.group("second"), m.groupdict(), m.start("first"), m.span("second"))
