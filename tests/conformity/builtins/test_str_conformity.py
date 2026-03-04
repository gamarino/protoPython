# Phase 1.1.2: str isolation — concatenation, slicing, methods return new roots.
# Immutability: no const_cast on string buffers; all ops return new roots.

assert isinstance("hello", str)
assert "a" + "b" == "ab"
assert "hello"[1:4] == "ell"
assert "  x  ".strip() == "x"
assert "a,b,c".split(",") == ["a", "b", "c"]

# Original unchanged (structural sharing / immutability)
s = "original"
t = s.strip()
assert s == "original"
assert "  original  ".strip() == "original"

print("OK str conformity")
