# str operations on non-ASCII text count, index and slice in code points, as
# len() does, not in UTF-8 bytes.
import json

s = "héllo wörld €10 𝄞!"
assert len(s) == 18

# Indexing and slicing.
assert s[1] == "é" and s[-2] == "𝄞" and s[-1] == "!"
assert s[0:5] == "héllo"
assert s[6:11] == "wörld"
assert s[12:] == "€10 𝄞!"
assert s[::-1] == "!𝄞 01€ dlröw olléh"
assert s[1:12:5] == "éw "
assert "é"[0] == "é" and "é"[-1] == "é"
try:
    "é"[1]
except IndexError:
    pass
else:
    raise AssertionError("expected IndexError")

# Iteration.
assert list("aé€𝄞") == ["a", "é", "€", "𝄞"]
assert [len(c) for c in "aé€𝄞"] == [1, 1, 1, 1]
assert [ord(c) for c in "é€"] == [233, 8364]

# Searching: indexes are code point indexes.
assert s.find("wörld") == 6
assert s.find("€") == 12 and s.index("€") == 12
assert s.rfind("l") == 9 and s.rindex("ö") == 7
assert s.find("r", 5) == 8
assert s.find("é", 2) == -1
assert s.count("l") == 3 and s.count("l", 4) == 1
assert "ééé".count("é") == 3
assert "é" in s and "€1" in s and "x" not in s
assert s.find("𝄞", 0, 17) == 16 and s.find("𝄞", 0, 16) == -1

# Prefix and suffix tests with offsets.
assert s.startswith("wö", 6) and not s.startswith("wö", 7)
assert s.endswith("€10", 0, 15)
assert s.startswith(("x", "hé"))

# Splitting, partitioning, stripping, replacing, joining.
assert s.split(" ") == ["héllo", "wörld", "€10", "𝄞!"]
assert "a€b€c".split("€") == ["a", "b", "c"]
assert "a€b€c".rsplit("€", 1) == ["a€b", "c"]
assert "a€b€c".partition("€") == ("a", "€", "b€c")
assert "a€b€c".rpartition("€") == ("a€b", "€", "c")
assert "  ñandú  ".strip() == "ñandú"
assert "éaé".strip("é") == "a" and "éaé".lstrip("é") == "aé" and "éaé".rstrip("é") == "éa"
assert "€€x€".replace("€", "EUR") == "EUREURxEUR"
assert "€€x€".replace("€", "", 2) == "x€"
assert "-".join(["é", "ö"]) == "é-ö"

# Case, alignment and repeat.
assert "é".center(5, "*") == "**é**" and "é".ljust(3) == "é  " and "é".rjust(3, "€") == "€€é"
assert "é" * 3 == "ééé" and len("é" * 3) == 3
assert "é".zfill(3) == "00é"
assert "€%s€" % "é" == "€é€" and "{}€".format("é") == "é€"

# repr and ascii.
assert repr("é") == "'é'"
assert repr("€\n") == "'€\\n'"
assert ascii("é€") == "'\\xe9\\u20ac'"
assert str(["é"]) == "['é']"

# json round trip of non-ASCII text followed by more JSON.
assert json.loads('["é", 1]') == ["é", 1]
assert json.loads('{"k": "€𝄞", "n": [1, 2]}') == {"k": "€𝄞", "n": [1, 2]}
assert json.loads(json.dumps({"ä": "ö"})) == {"ä": "ö"}
assert json.dumps("é") == '"\\u00e9"'

print("str_non_ascii: ok")
