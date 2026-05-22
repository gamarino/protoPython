"""Minimal unicodedata stub for protoPython."""

unidata_version = "15.1.0"

def lookup(name):
    raise KeyError(name)

def name(chr, default=None):
    if default is not None:
        return default
    raise ValueError(f"no such name")

def decimal(chr, default=None):
    val = ord(chr)
    if 0x30 <= val <= 0x39:
        return val - 0x30
    if default is not None:
        return default
    raise ValueError(f"not a decimal")

def digit(chr, default=None):
    return decimal(chr, default)

def numeric(chr, default=None):
    return decimal(chr, default)

def category(chr):
    o = ord(chr)
    if chr.isalpha():
        return "Lu" if chr.isupper() else "Ll"
    if chr.isdigit():
        return "Nd"
    if chr.isspace():
        return "Zs"
    if o < 0x20 or (0x7F <= o <= 0x9F):
        return "Cc"
    return "Po"

def bidirectional(chr):
    if chr.isalpha():
        return "L"
    if chr.isdigit():
        return "EN"
    return ""

def combining(chr):
    return 0

def east_asian_width(chr):
    o = ord(chr)
    if o < 0x80:
        return "Na"
    if 0x1100 <= o <= 0x115F:
        return "W"
    return "N"

def mirrored(chr):
    return 0

def decomposition(chr):
    return ""

def normalize(form, unistr):
    return unistr

def is_normalized(form, unistr):
    return True
