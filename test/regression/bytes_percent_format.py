# bytes % args (PEP 461) was unsupported: "unsupported operand type(s) for
# %: 'bytes' and 'tuple'".
assert b"%d-%s" % (3, b"x") == b"3-x"
assert b"%5.2f|%-4d|%04x|%X" % (1.5, 7, 255, 255) == b" 1.50|7   |00ff|FF"
assert b"%s" % b"one" == b"one"
assert b"%b" % bytearray(b"ba") == b"ba"
assert b"%s" % memoryview(b"mv") == b"mv"
assert b"%r %a" % ("\xe9", b"q") == b"'\\xe9' b'q'"
assert b"%c%c" % (65, b"B") == b"AB"
assert b"%5s|%-5s|%.2s" % (b"ab", b"cd", b"xyz") == b"   ab|cd   |xy"
assert b"%*d" % (4, 2) == b"   2"
assert b"%(a)s and %(b)d" % {b"a": b"A", b"b": 2} == b"A and 2"
assert b"100%%" % () == b"100%"


class HasBytes:
    def __bytes__(self):
        return b"hb"


assert b"%s" % HasBytes() == b"hb"


def raises(exc, f):
    try:
        f()
    except exc:
        return
    raise AssertionError(f"{exc.__name__} not raised")


raises(TypeError, lambda: b"%s" % "str")
raises(TypeError, lambda: b"%d %d" % (1,))
raises(TypeError, lambda: b"%d" % (1, 2))
raises(TypeError, lambda: b"%c" % b"ab")
raises(OverflowError, lambda: b"%c" % 256)
raises(ValueError, lambda: b"%y" % 1)
print("bytes_percent_format: ok")
