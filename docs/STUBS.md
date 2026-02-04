# protoPython Stub Status

This document catalogs stub implementations and their completion status.

## Native (C++) Stubs — Completed

| Component | Item | Status |
|-----------|------|--------|
| **set** | union, intersection, difference | Implemented |
| **itertools** | accumulate | Full implementation (default add, optional binary func) |
| **itertools** | groupby, product, combinations, combinations_with_replacement, permutations | Return empty iterator (no longer None) |
| **math** | isclose, log, log10, exp, sqrt, sin, cos, tan | Implemented |
| **operator** | add, sub, mul, truediv, eq, lt, pow, floordiv, mod, neg, not_, invert | Implemented |

## Native (C++) Stubs — Remaining

| Component | Item | Notes |
|-----------|------|-------|
| **builtins** | help, memoryview | Stub retained. Implementation deferred: full impl needs pager (help) and buffer API (memoryview). Return None. |
| **builtins** | input, eval, exec | Stub retained. Implementation deferred: need stdin (input) and expression/statement parser (eval, exec). Return None/empty. |
| **builtins** | breakpoint, globals, locals | Stub retained. Implementation deferred: need debugger integration (breakpoint) and frame access (globals, locals). breakpoint no-op; globals/locals return empty dict. |

## Python stdlib Stubs — Enhanced

| Module | Item | Status |
|--------|------|--------|
| **os** | environ, getcwd, chdir, path, pathsep, sep | Stub retained. environ empty dict (full impl requires native getenv); getcwd/chdir/path stub values; no-op where applicable. |
| **tempfile** | gettempdir, gettempprefix, mkstemp, TemporaryFile | gettempdir/gettempprefix implemented (from os or default '/tmp', 'tmp'); mkstemp returns (0, path); TemporaryFile stub. |
| **subprocess** | run, Popen, CompletedProcess | Stub retained. Full impl requires process creation API. run returns CompletedProcess; Popen no-op. |

## Python stdlib Stubs — Minimal (unchanged)

| Module | Item | Behavior |
|--------|------|----------|
| copy | copy, deepcopy | Implemented: shallow/deep for list, dict, tuple, set; else return x. Deep copy has no cycle detection. |
| hashlib | md5, sha1, sha256 | Stub retained. Return None. Full impl requires native hashing. |
| base64 | b64encode, b64decode | Implemented: RFC 4648; accepts bytes-like; returns bytes. |
| struct | pack, unpack | Implemented: b, h, i, l, q, f, d, s, ? (little-endian); minimal IEEE 754 for f/d. |
| getopt | getopt | Implemented: shortopts, longopts; returns (options, args). GetoptError on bad option. |
| argparse | ArgumentParser | Stub retained. add_argument/parse_args return defaults. Full parser deferred. |
| warnings | warn | Implemented: prints message (and category name if given) to sys.stderr when available. |
| time | time, sleep | Return 0.0; no-op |
| random | random, choice | Return 0.0; return first element |
| datetime | datetime | Stub class |
| uuid | uuid4 | Return fixed string |
| secrets | token_hex, token_urlsafe | Return placeholder |
| threading | Thread, Lock | Minimal stubs |
| enum | Enum | Implemented: name, value, __repr__; Enum(value) or Enum(name, value). auto() placeholder. |
| functools | reduce, wraps, lru_cache | reduce implemented; wraps copies __name__, __doc__, __module__, etc.; lru_cache stub (v20). |
| csv | reader, writer | Implemented: reader yields lists of strings; writer.writerow/writerows; delimiter default ','. |
| xml.etree | ElementTree, Element | Stub retained. Minimal stub; full XML parser out of scope. |

## Python stdlib — Additional catalogued

| Module | Item | Behavior |
|--------|------|----------|
| re | compile, match, search, findall, sub | Native ReModule; lib stub fallback returns None/[]/string. |
| atexit | register, unregister | No-op; does not register for exit. |
| heapq | heappush, heappop, heapify | Implemented: list as min-heap; heappush, heappop, heapify in-place. |
| io | StringIO | Minimal: getvalue, read, write. |
| typing | Any, List, Dict, etc. | Minimal stub for type hints. |
| unittest | TestCase, main | Minimal stub placeholders. |
| traceback | extract_tb, format_* | Minimal stub. |
| dataclasses | dataclass | Minimal decorator placeholder. |
| contextlib | contextmanager | Minimal placeholder. |
| abc | ABC, abstractmethod | Minimal placeholder. |

## Python stdlib — New stubs (v22)

| Module | Item | Behavior |
|--------|------|----------|
| html | escape, unescape | Minimal: escape replaces &<>\"'; unescape reverses. |
| difflib | SequenceMatcher, unified_diff | Stub: SequenceMatcher returns placeholder; unified_diff returns empty list. |

## Python stdlib — New stubs

| Module | Item | Behavior |
|--------|------|----------|
| statistics | mean, median, stdev | Stub: return 0.0. |
| decimal | Decimal | Stub class; stores value; minimal __str__. |
| fractions | Fraction | Stub class; stores n/d; minimal __str__. |
