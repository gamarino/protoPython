#!/usr/bin/env python3
"""Static check of the escaping boundary (docs/DESIGN_DECISIONS.md).

Internal keys start with U+0001. A name Python code supplies becomes a key
only through PythonEnvironment::pythonKeySpelling() and a stored key becomes
a Python name only through storedKeyPythonName(); every other helper is
built on that pair. This script fails when the C++ sources:

  * re-implement the rule: test the second code point of a key for U+0001,
    or prepend U+0001 to a run-time string, outside a line marked
    `escape-boundary`;
  * use a ProtoString they did not escape as an attribute key, as in
    `obj->getAttribute(ctx, name->asString(ctx))`, outside a line marked
    `raw-key:` with the reason (a name the compiler produced, which Python
    code cannot choose).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = sorted(list((ROOT / "src").rglob("*.cpp")) + list((ROOT / "include").rglob("*.h")))

ESCAPE = re.compile(r"""\[1\]\s*[!=]=\s*'\\(x01|001)'|"\\(x01|001)"\s*\+\s*[A-Za-z_]""")
RAW_KEY = re.compile(
    r"(get|set|has|remove|delete)[A-Za-z]*Attribute[A-Za-z]*\(\s*[A-Za-z_]+\s*,\s*"
    r"[A-Za-z_>()\-]+->asString\(\s*[A-Za-z_]+\s*\)")

problems = []
for path in SOURCES:
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        code = line.split("//", 1)[0] if "//" in line and '"' not in line.split("//", 1)[0] else line
        if ESCAPE.search(code) and "escape-boundary" not in line:
            problems.append(f"{path.relative_to(ROOT)}:{number}: escaping rule re-implemented: {line.strip()}")
        if RAW_KEY.search(code) and "raw-key:" not in line:
            problems.append(f"{path.relative_to(ROOT)}:{number}: unescaped attribute key: {line.strip()}")

for p in problems:
    print(p)
if problems:
    print(f"check_escape_boundary: {len(problems)} problem(s)")
    sys.exit(1)
print(f"check_escape_boundary: ok ({len(SOURCES)} files)")
