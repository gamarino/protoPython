# json.dumps escapes strings as CPython does. The pure-Python json encoder
# depends on re.sub with a replacement function and on matching str values per
# code point; the native re module ignored replacement functions and matched
# UTF-8 bytes.
import json
import re

# re matches code points and reports positions in code points.
assert re.search("b", "éb").start() == 1
assert re.search("[^a]", "aé").group(0) == "é"
assert re.findall(r"[^\ -~]", "aéb\n") == ["é", "\n"]
assert re.fullmatch("é+", "éé") is not None
assert re.compile("b").match("éb", 1).span() == (1, 2)
assert re.split(",", "é,b") == ["é", "b"]
assert re.sub("é", "e", "café") == "cafe"
assert re.escape("é.") == "é\\."

# re.sub and subn: replacement functions, templates and count.
assert re.sub("b", lambda m: "X", "abc") == "aXc"
assert re.sub("b", lambda m: None, "abc") == "ac"
assert re.compile("b").sub(lambda m: m.group(0).upper(), "abcb") == "aBcB"
assert re.subn("b", lambda m: "X", "abcb") == ("aXcX", 2)
assert re.compile("b").subn("X", "abb") == ("aXX", 2)
assert re.sub("b", "X", "abcb", count=1) == "aXcb"
assert re.compile("b").sub("X", "abcb", 1) == "aXcb"
assert re.sub(r"(a)(b)?", r"<\1\2>", "ac") == "<a>c"
assert re.sub(r"(?P<x>a)", r"<\g<x>\g<1>\g<0>>", "ab") == "<aaa>b"
assert re.sub("a", r"\n", "bab") == "b\nb"

# json.dumps with ensure_ascii (the default) and without it.
assert json.dumps("é\n") == '"\\u00e9\\n"'
assert json.dumps("é\n", ensure_ascii=False) == '"é\\n"'
assert json.dumps('a"b\\c\t') == '"a\\"b\\\\c\\t"'
assert json.dumps('a"b\\c\t', ensure_ascii=False) == '"a\\"b\\\\c\\t"'
assert json.dumps("\x01\x1f") == '"\\u0001\\u001f"'
assert json.dumps("€") == '"\\u20ac"'
assert json.dumps({"é": ["€", 1]}) == '{"\\u00e9": ["\\u20ac", 1]}'

# json.loads of escaped and raw non-ASCII text. Raw non-ASCII text followed by
# more JSON is not checked: str indexing still counts UTF-8 bytes (see
# docs/CPYTHON_CONFORMANCE.md).
assert json.loads('"\\u00e9\\n"') == "é\n"
assert json.loads('"é"') == "é"
assert json.loads(json.dumps({"é": "€"})) == {"é": "€"}

print("json_dumps_escapes: ok")
