# The re module (PCRE2 underneath) against CPython 3.14: every line printed
# here is compared with re_pcre2_semantics.expected, CPython's output for the
# same program.
#
# Covered: the translation of Python's syntax (named groups, back references,
# conditionals, lookarounds, scoped and global inline flags, verbose mode,
# quantifier forms, escapes, classes), the character sets of \w \d \s \b in
# Unicode and ASCII modes, IGNORECASE with and without ASCII, str and bytes,
# code point positions in non-ASCII subjects, pos / endpos, the results of
# every function and Match method, lastindex / lastgroup, replacement
# templates, Python's empty-match rule, re.error messages and positions, and
# re.Scanner.

import re
import warnings

# CPython warns (FutureWarning) about '[[' as a possible future set
# operation; the warning is not part of what is compared.
warnings.simplefilter("ignore")


def show(label, value):
    print(label, "->", repr(value))


def span_of(m):
    return None if m is None else (m.span(), m.group(0))


def err(label, fn):
    try:
        fn()
        print(label, "-> no error")
    except re.error as e:
        print(label, "-> re.error:", e.msg, "| pos", e.pos)
    except (ValueError, TypeError, OverflowError, IndexError) as e:
        print(label, "->", type(e).__name__ + ":", e)


# --- basic operations -------------------------------------------------------
show("search", span_of(re.search(r"\d+", "abc 123 def 45")))
show("match none", re.match(r"\d", "a1"))
show("fullmatch alt", span_of(re.fullmatch(r"a|ab", "ab")))
show("fullmatch lazy", span_of(re.fullmatch(r"a.*?", "abc")))
show("match prefix", span_of(re.match(r"a|ab", "ab")))
show("findall plain", re.findall(r"\w+", "one two  three"))
show("findall 1 group", re.findall(r"(\w)\w*", "one two three"))
show("findall n groups", re.findall(r"(\w)(\d)?", "a1b c3"))
show("finditer", [m.span() for m in re.finditer(r"o+", "foo boo zoooo")])
show("split", re.split(r"[,;]\s*", "a, b;c,  d"))
show("split groups", re.split(r"([,;])", "a,b;c"))
show("split optional group", re.split(r"(,)|(;)", "a,b;c"))
show("split maxsplit", re.split(r",", "a,b,c,d", maxsplit=2))
show("split empty", re.split(r"x*", "axbc"))
show("split boundary", re.split(r"\b", "a b"))
show("sub", re.sub(r"\s+", " ", "a   b \t c"))
show("sub count", re.sub(r"a", "b", "aaaa", count=2))
show("subn", re.subn(r"\d", "#", "a1b22c"))
show("sub empty", re.sub(r"x*", "-", "abxd"))
show("sub callable", re.sub(r"\d+", lambda m: str(int(m.group()) * 2), "a1 b20 c300"))
show("sub template", re.sub(r"(\w+)@(\w+)", r"\2 at \1", "user@host"))
show("sub named", re.sub(r"(?P<k>\w+)=(?P<v>\w+)", r"\g<v>=\g<k>", "a=1 b=2"))
show("sub \\g<0>", re.sub(r"\d", r"[\g<0>]", "a1b2"))
show("sub escapes", re.sub(r"x", r"\n\t\\", "axb"))
show("sub unmatched group", re.sub(r"(a)|b", r"[\1]", "ab"))
show("sub octal", re.sub(r"x", r"\101\0", "x"))
show("escape", re.escape("a.b*c?d[e]f{g}h(i)j|k^l$m\\n o#p-q&r~s\tt"))
show("escape bytes", re.escape(b"a.b c"))
show("escape unicode", re.escape("é ñ_1"))

# --- match objects ------------------------------------------------------------
m = re.search(r"(?P<year>\d{4})-(?P<month>\d\d)(-(?P<day>\d\d))?", "on 2026-10 and")
show("group()", m.group())
show("group(1,2)", m.group(1, 2))
show("group name", m.group("month"))
show("groups", m.groups())
show("groups default", m.groups("-"))
show("groupdict", m.groupdict())
show("groupdict default", m.groupdict(0))
show("span/start/end", (m.span(), m.start(2), m.end("year"), m.span(3)))
show("lastindex/lastgroup", (m.lastindex, m.lastgroup))
show("getitem", (m[0], m["year"], m[2]))
show("expand", m.expand(r"\g<month>/\g<year> [\3]"))
show("pos/endpos/string", (m.pos, m.endpos, m.string))
show("re.pattern", m.re.pattern)
show("repr", repr(m))
err("group out of range", lambda: m.group(9))
err("group bad name", lambda: m.group("nope"))
for pat, s in [(r"(a)(b)", "ab"), (r"(a(b))", "ab"), (r"((a)b)", "ab"),
               (r"(a)|(b)", "b"), (r"(a)(b)?", "a"), (r"a", "a"),
               (r"((a)|(b))+", "ab"), (r"(?:(a)|b)*", "ab"), (r"(a)(?:x|(b))", "ab")]:
    mm = re.match(pat, s)
    show("lastindex " + pat, (mm.lastindex, mm.lastgroup, mm.groups()))
mm = re.match(r"(?P<outer>(?P<inner>a))", "a")
show("lastgroup nested", (mm.lastindex, mm.lastgroup))

# --- positions over non-ASCII subjects ------------------------------------------
s = "añb€c😀d"
show("non-ascii search", span_of(re.search(r"€c", s)))
show("non-ascii finditer", [m.span() for m in re.finditer(r".", s)])
show("non-ascii pos", span_of(re.compile(r"\w").search(s, 3)))
show("non-ascii endpos", span_of(re.compile(r"\w+$").search(s, 0, 4)))
show("non-ascii split", re.split(r"[€😀]", s))
show("non-ascii sub", re.sub(r"[^a-z]", "_", s))
show("emoji class", re.findall(r"[😀-😂]", "x😀y😁z😃"))

# --- pos / endpos -------------------------------------------------------------
p = re.compile(r"^\w+$|x")
show("pos anchors", span_of(p.search("ab cd", 3)))
show("endpos anchors", span_of(p.search("ab cd", 0, 2)))
show("pos > len", p.search("abc", 10))
show("negative pos", span_of(p.search("abc", -5)))
show("endpos < pos", p.search("abc", 2, 1))
show("match pos", span_of(re.compile(r"c").match("abc", 2)))
show("fullmatch window", span_of(re.compile(r"b+").fullmatch("abbbc", 1, 4)))
show("findall window", re.compile(r"\w").findall("abcdef", 1, 4))
show("\\A with pos", re.compile(r"\Ab").search("ab", 1))
show("^ with pos", re.compile(r"^b").search("ab", 1))

# --- character sets: Unicode mode ----------------------------------------------
# (A string, split: a literal list of this length exceeds protopy's
# expression stack.)
samples = [int(x, 16) for x in """
    09 0B 1C 1F 20 41 5F 85 A0 AA B2 B5 BA C0 300 345 660 669 2028 2029
    2000 200B 202F 205F 3000 2160 2070 FF10 FF21 1D7CE 10400 203F 2163 B9
    BF1 16EE""".split()]
for esc in [r"\w", r"\W", r"\d", r"\D", r"\s", r"\S", r"[\w]", r"[\s]", r"[\d]",
            r"[^\w]", r"[\S]", r"[\D]", r"[\s\d]", r"[^\s]"]:
    for flags, name in [(0, "U"), (re.ASCII, "A")]:
        rx = re.compile(esc, flags)
        bits = "".join("1" if rx.fullmatch(chr(c)) else "0" for c in samples)
        print("class", esc, name, bits)
for flags, name in [(0, "U"), (re.ASCII, "A")]:
    show("\\b " + name, [m.span() for m in re.finditer(r"\b", "é a_1 ñ", flags)])
    show("\\B " + name, [m.span() for m in re.finditer(r"\B", "éa b", flags)])

# --- IGNORECASE ---------------------------------------------------------------
show("i ascii", span_of(re.search(r"hello", "HeLLo", re.I)))
show("i unicode", span_of(re.search(r"straße é", "STRAßE É", re.I)))
show("i class", re.findall(r"[a-c]+", "ABCabcXYZ", re.I))
show("i A literal", span_of(re.search(r"é", "É", re.I | re.A)))
show("i A ascii", span_of(re.search(r"k", "K", re.I | re.A)))
show("i A class", re.findall(r"[a-cé]", "ABCÉé", re.I | re.A))
show("i A range", re.findall(r"[X-c]", "xyzXYZ[]^_`abcABC", re.I | re.A))
show("i A negated", re.findall(r"[^a]", "aAb", re.I | re.A))
show("i kelvin", span_of(re.search(r"k", "\u212a", re.I)))
show("i bytes", re.findall(rb"[a-c]+", b"ABCabc\xc9", re.I))
show("scoped i", (span_of(re.match(r"(?i:a)b", "Ab")), re.match(r"(?i:a)b", "aB")))
show("scoped -i", span_of(re.match(r"(?-i:a)b", "aB", re.I)))
show("nested i", (span_of(re.match(r"(?i:(?-i:a)b)", "aB")), re.match(r"(?i:(?-i:a)b)", "AB")))

# --- inline flags ---------------------------------------------------------------
show("(?m)", re.findall(r"(?m)^\w", "ab\ncd\n"))
show("(?s)", span_of(re.search(r"(?s)a.b", "a\nb")))
show("no s", re.search(r"a.b", "a\nb"))
show("(?x)", span_of(re.search(r"(?x) a \  b  # comment", "a b")))
show("(?x) class", span_of(re.search(r"(?x)[ ]a", " a")))
show("(?x:)", span_of(re.search(r"(?x: a b ) c", "ab c")))
show("(?a)", re.findall(r"(?a)\w+", "héllo wörld"))
show("(?i)(?m)", re.findall(r"(?i)(?m)^a", "A\na"))
show("flags attr", (re.compile("x").flags, re.compile("(?i)x").flags,
                    re.compile("x", re.A).flags, re.compile(b"x").flags,
                    re.compile(b"(?i)x", re.M).flags))
show("scoped a", span_of(re.match(r"\w(?a:\W)\w", "\xe0\xe0\xe0")))
show("scoped u", span_of(re.match(r"(?a:\W(?u:\w)\W)", "\xe0\xe0\xe0")))
show("scoped u in A", span_of(re.match(r"\W(?u:\w)\W", "\xe0\xe0\xe0", re.ASCII)))
show("scoped a \\b", [m.span() for m in re.finditer(r"(?a:\b)", "é a")])
show("scoped m", re.findall(r"(?m:^a)|^b", "b\na\nb"))
show("MULTILINE $", re.findall(r"\w$", "a\nb\n", re.M))
show("$ before final newline", span_of(re.search(r"a$", "a\n")))
show("\\Z", re.search(r"a\Z", "a\n"))
show("^ empty lines", re.findall(r"^", "a\nb\n", re.M))
show("$ positions", [m.span() for m in re.finditer(r"$", "a\nb\n", re.M)])

# --- quantifiers, groups, assertions --------------------------------------------
show("{m,n}", re.findall(r"a{2,3}", "aaaaaaa"))
show("{,n}", re.findall(r"a{,2}", "aaa"))
show("{m,}", span_of(re.search(r"a{2,}", "aaaa")))
show("{} literal", span_of(re.search(r"a{", "a{")))
show("{x} literal", span_of(re.search(r"a{x}", "a{x}")))
show("{1,x} literal", span_of(re.search(r"a{1,x}", "a{1,x}")))
show("lazy", re.findall(r"<.+?>", "<a><b>"))
show("possessive", (re.search(r"a++b", "aaab").span(), re.search(r"a++a", "aaa")))
show("atomic", (re.search(r"(?>a+)b", "aab").span(), re.search(r"(?>a+)a", "aaa")))
show("lookahead", re.findall(r"\w+(?=,)", "a,b,c"))
show("neg lookahead", re.findall(r"\b\w(?!\d)", "a1 b c2"))
show("lookbehind", re.findall(r"(?<=\$)\d+", "$10 20 $30"))
show("neg lookbehind", re.findall(r"(?<!\$)\b\d+", "$10 20 $30"))
show("backref", span_of(re.search(r"(\w)\1", "abccd")))
show("named backref", span_of(re.search(r"(?P<c>\w)(?P=c)", "abccd")))
show("backref 10+", span_of(re.search(r"(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)\10", "abcdefghijj")))
show("conditional", [re.match(r"(<)?\w+(?(1)>)$", s) is not None for s in ["<a>", "a", "<a", "a>"]])
show("conditional name", [bool(re.match(r"(?P<q>')?\w(?(q)')$", s)) for s in ["'a'", "a", "'a"]])
show("conditional forward", bool(re.match(r"(?(2)a|b)(x)?(y)?", "b")))
show("comment group", span_of(re.search(r"a(?#comment)b", "ab")))
show("alternation empty", re.findall(r"a|", "ab"))
show("nested quantified group", re.match(r"((a)|b)+", "abab").groups())
show("octal escapes", (span_of(re.search(r"\101", "A")), span_of(re.search(r"\0", "\0")),
                       span_of(re.search(r"[\101]", "A"))))
show("hex/unicode escapes", (span_of(re.search(r"\x41\u00e9\U0001F600", "Aé😀")),
                             span_of(re.search(r"[\x41-\x43]+", "ABCD"))))
show("control escapes", span_of(re.search(r"\a\f\n\r\t\v", "\a\f\n\r\t\v")))
show("class edge chars", re.findall(r"[]a-]", "]a-b"))
show("class caret", re.findall(r"[a^]", "^ab"))
show("class bracket", re.findall(r"[[]", "a[b"))
show("class backspace", span_of(re.search(r"[\b]", "a\bb")))
show("class escapes", re.findall(r"[\]\-\\]", "]-\\x"))
show("dot newline", re.findall(r".", "a\nb"))
show("surrogate pattern", re.search("\ud800", "abc"))
show("surrogate class", re.findall("[\ud800-\udfff]", "abc"))
show("surrogate range", re.findall("[a-\U0001F600]", "b\U0001F600"))

# --- empty-match rule ------------------------------------------------------------
show("empty finditer", [m.span() for m in re.finditer(r"\b|x", "ab x")])
show("empty findall", re.findall(r"a*", "baac"))
show("empty sub", re.sub(r"a*", "-", "baac"))
show("empty split", re.split(r"a*", "baac"))
show("empty after nonempty", [m.span() for m in re.finditer(r"a|(?=b)", "ab")])

# --- bytes ---------------------------------------------------------------------
show("bytes search", span_of(re.search(rb"\d+", b"ab12")))
show("bytes findall", re.findall(rb"\w+", b"ab \xe9t\xe9 cd"))
show("bytes sub", re.sub(rb"(\w)", rb"<\1>", b"ab"))
show("bytes split", re.split(rb",", b"a,b"))
show("bytes groupdict", re.match(rb"(?P<x>a)", b"a").groupdict())
show("bytes high", re.findall(rb"[\x80-\xff]+", b"a\x80\xffb"))
show("bytearray", span_of(re.search(rb"b+", bytearray(b"abbc"))))
show("bytes \\s", re.findall(rb"\s", b" \t\n\x0b\x0c\r\x1c\x85\xa0"))
show("bytes dot", re.findall(rb".", b"a\nb"))
err("bytes on str", lambda: re.search(rb"a", "a"))
err("str on bytes", lambda: re.search(r"a", b"a"))
err("str repl for bytes", lambda: re.sub(rb"a", "b", b"a"))
err("not a string", lambda: re.search(r"a", 5))
err("callable returns int", lambda: re.sub(r"a", lambda m: 1, "a"))

# --- compiled patterns and the cache -----------------------------------------------
p1 = re.compile(r"ab+", re.I)
show("pattern attrs", (p1.pattern, p1.flags, p1.groups, dict(re.compile(r"(?P<a>x)(y)(?P<b>z)").groupindex)))
show("pattern repr", (repr(p1), repr(re.compile(b"x", re.M)), repr(re.compile("x", re.A | re.S))))
show("compile of compiled", re.compile(p1) is p1)
err("flags with compiled", lambda: re.compile(p1, re.M))
show("pattern equality", (re.compile("a") == re.compile("a"), re.compile("a") == re.compile("a", re.I),
                          re.compile("a") == re.compile(b"a"), hash(re.compile("q")) == hash(re.compile("q"))))
show("module search with compiled", span_of(re.search(p1, "xABBB")))
re.purge()
show("after purge", span_of(re.search(r"ab+", "xabb")))
sc = re.compile(r"\d").scanner("a1b22")
show("scanner search", [sc.search().group() for _ in range(3)] + [sc.search()])
sc = re.compile(r"\d").scanner("12a")
show("scanner match", [span_of(sc.match()), span_of(sc.match()), sc.match()])

# --- errors: messages and positions ----------------------------------------------
ERROR_PATTERNS = []
ERROR_PATTERNS += ['(', '((a)', 'a)', '*a', 'a**', 'a{2}{3}', '[a', '[z-a]', '\\q', '(?P<1a>x)', '(?P<a>x)(?P<a>y)', '(?P=nope)', '\\2(a)', '(a\\1)', '(?z)']
ERROR_PATTERNS += ['(?iz)', '(?i', '(?-i)', 'a(?i)b', '(?<', '(?<>x)', '(?P', '(?Px)', '(?#abc', 'x{3,2}', '(?(1)a|b|c)', '(?(x)a)', '(?(0)a)', '\\x4', '\\u12']
ERROR_PATTERNS += ['\\U00110000', '[\\w-z]', '\\8', '\\777', '(?au:x)', '(?a)(?-a:\\w)', '(?i-i:a)', '(?-', 'a\n(', '(?P<a>a)(?P=a', '\\\\', '(a)b(?<=(.)\\2)(c)', '(a)b(?<=(?(2)b|x))(c)', '(?R)', '(?1)(a)']
ERROR_PATTERNS += ['(?&n)', '(?|a)', '(*FAIL)', '\\K', '\\Qa\\E', '\\p{L}']
for pat in ERROR_PATTERNS:
    err("error " + repr(pat), lambda: re.compile(pat))
err("bytes \\u", lambda: re.compile(rb"\u0041"))
err("bytes (?u)", lambda: re.compile(rb"(?u)x"))
err("str (?L)", lambda: re.compile(r"(?L)x"))
err("LOCALE str", lambda: re.compile("x", re.LOCALE))
err("ASCII|UNICODE", lambda: re.compile("x", re.A | re.U))
err("inline a with U", lambda: re.compile(r"(?a)x", re.U))
err("UNICODE bytes", lambda: re.compile(b"x", re.U))
err("repeat overflow", lambda: re.compile(r"a{%d}" % 2**40))
for repl in [r"\g<", r"\g<>", r"\g<x", r"\g<1a>", r"\g<9>", r"\3", r"\q", "\\", r"\g<nope>"]:
    err("template " + repr(repl), lambda: re.sub(r"(a)(b)", repl, "ab"))
try:
    re.compile("ab\n(")
except re.error as e:
    show("error lineno/colno", (e.lineno, e.colno, e.pattern))
show("error alias", re.error is re.PatternError and issubclass(re.error, Exception))

# --- re.Scanner -----------------------------------------------------------------
scanner = re.Scanner([
    (r"[a-z_]\w*", lambda s, t: ("NAME", t)),
    (r"(\d+)(\.\d+)?", lambda s, t: ("NUM", t, s.match.group(2))),
    (r"\s+", None),
    (r"[+*/-]", lambda s, t: ("OP", t)),
    (r"=", "ASSIGN"),
])
show("Scanner", scanner.scan("x = 3.5 + y_1*42 ?rest"))

# --- sub replacement types ---------------------------------------------------------
show("str repl, bytes pattern, no match", re.sub(rb"a", "b", b"x"))
err("str repl, bytes pattern, match", lambda: re.sub(rb"a", "b", b"xa"))
err("bytes repl, str pattern", lambda: re.sub(r"a", b"b", "xa"))
err("int repl", lambda: re.sub(r"a", 5, "xa"))
show("callable returns None", re.sub(r"a", lambda m: None, "xaya"))
err("callable returns int later", lambda: re.sub(r"a", lambda m: 1, "xaya"))
show("sub no match is subject", re.sub(r"q", "z", "a long subject string") == "a long subject string")
