# \N{NAME} escapes in string literals, as CPython 3 decodes them: character
# names in any letter case, aliases, the computed Hangul and CJK names, and a
# SyntaxError "(unicode error) 'unicodeescape' ..." for a malformed escape or
# an unknown name.  Unknown escapes keep their backslash.  Expected values are
# CPython 3.14's (Unicode 16.0.0).


def syntax_error(source):
    try:
        eval(source)
    except SyntaxError as e:
        return e.msg
    return None


assert "\N{LATIN SMALL LETTER A}" == "a"
assert "\N{latin small letter a}" == "a"
assert "\N{EM DASH}" == "—" and "\N{BULLET}" == "•"
assert "\N{GREEK SMALL LETTER ALPHA}\N{GREEK SMALL LETTER BETA}" == "αβ"
assert "x\N{SPACE}y" == "x y"
assert "\N{SNOWMAN}" == "☃" and "\N{GRINNING FACE}" == "\U0001F600"
assert "\N{EGYPTIAN HIEROGLYPH A001}" == "\U00013000"
assert """\N{BULLET} item""" == "• item"
# aliases: control names, abbreviations, corrections
assert "\N{NULL}" == "\x00" and "\N{LINE FEED}" == "\n" and "\N{LF}" == "\n"
assert "\N{NBSP}" == "\xa0" and "\N{BYTE ORDER MARK}" == "﻿"
# computed names (upper case only) and PREFIX-XXXX ranges
assert "\N{HANGUL SYLLABLE GA}" == "가" and "\N{HANGUL SYLLABLE GAG}" == "각"
assert "\N{HANGUL SYLLABLE HIH}" == "힣"
assert "\N{CJK UNIFIED IDEOGRAPH-4E00}" == "一"
assert "\N{CJK UNIFIED IDEOGRAPH-20000}" == "\U00020000"
assert "\N{CJK COMPATIBILITY IDEOGRAPH-F900}" == "豈"
assert "\N{nushu character-1b170}" == "\U0001b170"
assert "\N{EGYPTIAN HIEROGLYPH-13460}" == "\U00013460"
# f-strings; an escaped brace is a literal brace
assert f"\N{EM DASH}{1}\N{EM DASH}" == "—1—"
assert f"{1}\N{LEFT CURLY BRACKET}{2}" == "1{2"
assert f"{{\N{BULLET}}}" == "{•}"
assert f"\x7b\x7d" == "{}"
# not an escape: raw strings and bytes keep the text
assert r"\N{BULLET}" == "\\N{BULLET}"
assert b"\N{BULLET}" == b"\\N{BULLET}"
assert "\q" == "\\q" and "\d+" == "\\d+" and b"\q" == b"\\q"
assert "\'" == "'" and '\"' == '"'

UNKNOWN = "(unicode error) 'unicodeescape' codec can't decode bytes in position %s: unknown Unicode character name"
MALFORMED = "(unicode error) 'unicodeescape' codec can't decode bytes in position %s: malformed \\N character escape"
for source, expected in [
        (r'"\N{FOO}"', UNKNOWN % "0-6"),
        (r'"ab\N{FOO BAR}"', UNKNOWN % "2-12"),
        (r'"\x41\N{FOO}"', UNKNOWN % "4-10"),        # an escape counts as written
        ('"é\\N{FOO}"', UNKNOWN % "10-16"),   # a non-ASCII character counts 10
        (r'f"{1}\N{FOO}"', UNKNOWN % "0-6"),
        (r'"\N{cjk unified ideograph-4e00}"', UNKNOWN % "0-29"),
        (r'"\N{CJK UNIFIED IDEOGRAPH-0041}"', UNKNOWN % "0-29"),
        (r'"\N{hangul syllable ga}"', UNKNOWN % "0-21"),
        (r'"\N{LATIN  SMALL LETTER A}"', UNKNOWN % "0-24"),
        (r'"\N{KEYCAP NUMBER SIGN}"', UNKNOWN % "0-21"),     # a named sequence
        (r'"\N"', MALFORMED % "0-1"),
        (r'"\Nabc"', MALFORMED % "0-1"),
        (r'"\N{"', MALFORMED % "0-2"),
        (r'"\N{}"', MALFORMED % "0-2"),
        (r'"\N{LATIN SMALL LETTER A"', MALFORMED % "0-22"),
        ]:
    got = syntax_error(source)
    assert got == expected, (source, got)

print("ok")
