#!/usr/bin/env python3
"""Static check of the escaping boundary (docs/DESIGN_DECISIONS.md).

Internal keys start with U+0001. A name Python code supplies becomes a key
only through PythonEnvironment::pythonKeySpelling() and a stored key becomes
a Python name only through storedKeyPythonName(); every other helper is
built on that pair (pythonAttributeKey, pythonLookupKey, pythonAttributeName,
attributeKeySpelling).

The check works on statements, not lines: comments and line breaks are
removed and the source is cut at ';', '{' and '}' outside parentheses, so a
call split over several lines is one unit. It fails when a statement

  * re-implements the rule: compares a character with U+0001 ('\\x01',
    '\\001' next to == or !=), or holds the one-character string "\\x01" /
    "\\001" (a prefix being built);
  * passes, as an argument of a protoCore attribute call
    (get/set/has/remove/delete...Attribute...), a key made from a run-time
    string: `->asString(...)`, or `getInternedString`, `createSymbol`,
    `fromUTF8String` of anything but a string literal. A plain identifier
    argument is followed to its last assignment in the preceding 80 lines of
    the same file (one level: `auto k = name->asString(ctx);` then
    `getAttribute(ctx, k)`), and the branches of a ?: are checked each.
    A string literal, a macro's #name, or a `const char*` variable declared
    in the 400 lines above (a name out of a C++ table: Python strings reach
    C++ as ProtoString or std::string) is a C++ name, not a run-time string.

A statement is exempt when one of its lines carries `escape-boundary` (the
two functions of the rule) or `raw-key:` followed by the reason the string
cannot come from Python code (a compiler-made name, a C++ table of names).

What it does not see: a key built in another function and passed in as a
parameter or returned (the parameter is an identifier with no assignment in
view), a key kept in a member or a container, macros, and keys passed to
helpers whose names do not end in Attribute. Those are covered by review and
by the boundary tests in test/regression, not by this script.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROBE = None
if len(sys.argv) > 2 and sys.argv[1] == "--probe":
    # Self-test: every line of the probe file marked `// expect` must be
    # reported, and nothing else.
    PROBE = pathlib.Path(sys.argv[2]).resolve()
    ROOT = PROBE.parent
    SOURCES = [PROBE]
else:
    if len(sys.argv) > 1:
        ROOT = pathlib.Path(sys.argv[1]).resolve()
    SOURCES = sorted(list((ROOT / "src").rglob("*.cpp")) + list((ROOT / "src").rglob("*.h"))
                     + list((ROOT / "include").rglob("*.h")))

MARKERS = ("escape-boundary", "raw-key:")
CHAR_U1 = re.compile(r"(==|!=)\s*'\\(x0*1|0*01)'|'\\(x0*1|0*01)'\s*(==|!=)")
STR_U1 = re.compile(r'"\\(x0*1|0*01)"')
ATTR_CALL = re.compile(r"\b(get|set|has|remove|delete)[A-Za-z]*Attribute[A-Za-z]*\s*\(")
SAFE = re.compile(r"\b(pythonKeySpelling|pythonAttributeKey|pythonLookupKey|pythonAttributeName)\b")
LITERAL = re.compile(r'^\s*((u8)?"(\\.|[^"\\])*"|#[A-Za-z_]+)\s*$')
MAKER_CALL = re.compile(r"^\s*(?:[A-Za-z_][A-Za-z0-9_]*\s*(?:::|->|\.)\s*)*"
                        r"(getInternedString|createSymbol|ProtoString::fromUTF8String|getInternalString)\s*\(")
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def blank_comments(text):
    """Comments become spaces (newlines kept); literals are kept as they are."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            # Raw strings R"x(...)x" are skipped whole.
            if c == '"' and i > 0 and text[i - 1] == "R":
                m = re.match(r'"([^(\s]*)\(', text[i:])
                if m:
                    end = text.find(")" + m.group(1) + '"', i)
                    end = n if end < 0 else end + len(m.group(1)) + 2
                    # Generated text, not code of this file: only its lines stay.
                    out.append('""' + "\n" * text[i:end].count("\n"))
                    i = end
                    continue
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def statements(code):
    """(start_line, end_line, text) for each statement."""
    depth, start, line, buf, start_line = 0, 0, 1, [], 1
    i, n = 0, len(code)
    while i < n:
        c = code[i]
        if c in "\"'":
            j = i + 1
            while j < n and code[j] != c and code[j] != "\n":
                j += 2 if code[j] == "\\" else 1
            buf.append(code[i:j + 1])
            i = j + 1
            continue
        if c == "\n":
            line += 1
            buf.append(" ")
        elif c == "(" or c == "[":
            depth += 1
            buf.append(c)
        elif c == ")" or c == "]":
            depth = max(0, depth - 1)
            buf.append(c)
        elif c in "{}" or (c == ";" and depth == 0):
            # A brace always ends a unit, also inside parentheses: the body
            # of a lambda passed as an argument holds statements of its own.
            text = "".join(buf).strip()
            if text:
                yield start_line, line, text
            buf, start_line, depth = [], line, 0
        else:
            if not buf or (not "".join(buf).strip() and not c.isspace()):
                start_line = line
            buf.append(c)
        i += 1


def call_args(text, open_index):
    """Top-level arguments of the call whose '(' is at open_index."""
    args, depth, cur = [], 0, []
    i = open_index + 1
    while i < len(text):
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < len(text) and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append("".join(cur).strip())
                return args
            depth -= 1
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return args


def closes_at_end(text, open_index):
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return not text[i + 1:].strip()
    return False


def split_ternary(expr):
    depth, q = 0, -1
    for i, c in enumerate(expr):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "?" and depth == 0 and q < 0:
            q = i
        elif c == ":" and depth == 0 and q >= 0 and expr[i - 1:i + 1] != "::" and expr[i:i + 2] != "::":
            return [expr[q + 1:i], expr[i + 1:]]
    return [expr]


def c_string_name(lines, upto, source):
    """True when `source` is a `const char*` variable (or an element of a
    `const char*` array) declared in the 400 lines above: Python strings
    reach C++ as ProtoString or std::string, so a bare C string is a name
    out of a C++ table or literal (a `.c_str()` of a std::string is not one
    of these and stays flagged)."""
    m = re.match(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*$", source)
    if not m or lines is None:
        return False
    decl = re.compile(r"\bconst\s+char\s*\*\s*(const\s*)?(\*\s*(const\s*)?)?" + re.escape(m.group(1)) + r"\b")
    return any(decl.search(lines[k - 1]) for k in range(upto, max(0, upto - 400), -1))


def made_from_runtime_string(expr, lines=None, upto=0):
    """Why `expr` is a key made from a run-time string, or None."""
    if SAFE.search(expr):
        return None
    for branch in split_ternary(expr):
        b = branch.strip()
        if re.search(r"->\s*asString\s*\([^()]*\)\s*$", b):
            return "asString() of a value"
        # The key is the made string itself: the whole expression is the
        # maker's call (a value made from one, as setAttribute's third
        # argument, or a call taking one, is not a key).
        m = MAKER_CALL.match(b)
        if m:
            open_index = b.index("(", m.end() - 1)
            args = call_args(b, open_index)
            if not closes_at_end(b, open_index):
                continue
            source = args[1] if len(args) > 1 else (args[0] if args else "")
            if source and not all(LITERAL.match(x) or c_string_name(lines, upto, x) for x in split_ternary(source)):
                return f"{m.group(1)}() of a run-time string"
    return None


def definition(lines, upto, name):
    """The expression last assigned to `name` above line `upto` (1-based)."""
    pat = re.compile(r"(?<![A-Za-z0-9_.>])" + re.escape(name) + r"\s*(=(?!=)|\(|\{)\s*(.*)$")
    for k in range(upto - 1, max(0, upto - 81), -1):
        m = pat.search(lines[k - 1])
        if m:
            rhs = m.group(2)
            # Up to the end of the statement, which may continue below.
            j = k
            while ";" not in rhs and j < upto - 1 and j - k < 6:
                rhs += " " + lines[j].strip()
                j += 1
            return rhs.split(";")[0]
    return None


problems = []
for path in SOURCES:
    raw = path.read_text(encoding="utf-8", errors="replace")
    raw_lines = raw.splitlines()
    code = blank_comments(raw)
    code_lines = code.splitlines()
    rel = path.relative_to(ROOT)
    for first, last, stmt in statements(code):
        marked = any(mk in raw_lines[k - 1] for k in range(first, min(last, len(raw_lines)) + 1) for mk in MARKERS)
        if marked:
            continue
        if CHAR_U1.search(stmt) or STR_U1.search(stmt):
            problems.append(f"{rel}:{first}: escaping rule re-implemented: {stmt[:160]}")
        for m in ATTR_CALL.finditer(stmt):
            for arg in call_args(stmt, m.end() - 1)[1:]:
                why = made_from_runtime_string(arg, code_lines, last)
                if not why and IDENT.match(arg):
                    rhs = definition(code_lines, last, arg)
                    if rhs:
                        why = made_from_runtime_string(rhs, code_lines, last)
                        if why:
                            why += f" (through '{arg}')"
                if why:
                    problems.append(f"{rel}:{first}: unescaped attribute key, {why}: {stmt[:160]}")

problems = list(dict.fromkeys(problems))
if PROBE:
    lines = PROBE.read_text(encoding="utf-8").splitlines()
    expected = {f"{PROBE.name}:{i}" for i, l in enumerate(lines, 1) if "// expect" in l}
    reported = {p.split(": ", 1)[0] for p in problems}
    for miss in sorted(expected - reported):
        print(f"check_escape_boundary --probe: not reported: {miss}")
    for extra in sorted(reported - expected):
        print(f"check_escape_boundary --probe: reported in error: {extra}")
    if expected != reported:
        sys.exit(1)
    print(f"check_escape_boundary --probe: ok ({len(expected)} bypass forms reported)")
    sys.exit(0)
for p in problems:
    print(p)
if problems:
    print(f"check_escape_boundary: {len(problems)} problem(s)")
    sys.exit(1)
print(f"check_escape_boundary: ok ({len(SOURCES)} files)")
