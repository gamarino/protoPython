# protoPython Stub Status

This document catalogs stub implementations and their completion status.

## Native (C++) Stubs — Completed

| Component | Item | Status |
|-----------|------|--------|
| **set** | union, intersection, difference | Implemented |
| **itertools** | accumulate | Full implementation (default add, optional binary func) |
| **itertools** | groupby, product, combinations, combinations_with_replacement, permutations | Return empty iterator (no longer None) |
| **math** | isclose, log, log10, log2, log1p, exp, sqrt, sin, cos, tan, asin, acos, atan, atan2, degrees, radians, hypot, fmod, remainder, erf, erfc, gamma, lgamma, dist, perm, comb, factorial, prod, isqrt, acosh, asinh, atanh | Implemented |
| **operator** | add, sub, mul, truediv, eq, lt, pow, floordiv, mod, neg, not_, invert, lshift, rshift, and_, or_, xor, index | Implemented |

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

## Python stdlib — New stubs (v23)

| Module | Item | Behavior |
|--------|------|----------|
| shutil | copyfile, rmtree, copy, move | Stub: no-op. Full impl requires native file APIs. |
| mimetypes | guess_type, guess_extension, guess_all_extensions | Stub: return (None, None), None, []. |

## Python stdlib — New stubs (v24)

| Module | Item | Behavior |
|--------|------|----------|
| email | message_from_string | Stub: returns None. Full impl requires MIME parser. |
| runpy | run_path, run_module | Stub: return {}. Full impl requires import/exec machinery. |

## Python stdlib — New stubs (v25)

| Module | Item | Behavior |
|--------|------|----------|
| urllib.parse | quote, unquote, urljoin | Stub: return string/url unchanged. Full impl requires percent-encoding. |
| zoneinfo | ZoneInfo | Stub class with key; __str__, __repr__. Full impl requires tzdata. |

## Python stdlib — New stubs (v26)

| Module | Item | Behavior |
|--------|------|----------|
| plistlib | load, loads, dump, dumps | Stub: load/loads return empty dict; dump/dumps no-op/empty bytes. Full impl requires plist parser. |
| quopri | encode, decode | Stub: copy input to output. Full impl requires quoted-printable encoding. |

## Python stdlib — New stubs (v27)

| Module | Item | Behavior |
|--------|------|----------|
| binascii | hexlify, unhexlify, b2a_base64, a2b_base64 | Stub: return empty bytes. Full impl requires hex/base64 encoding. |
| uu | encode, decode | Stub: no-op. Full impl requires uuencode/uudecode. |

## Python stdlib — New stubs (v28)

| Module | Item | Behavior |
|--------|------|----------|
| configparser | ConfigParser: read, sections, get | Stub: read no-op; sections returns []; get returns fallback or "". |
| getpass | getpass, getuser | Stub: return "" or placeholder. Full impl requires tty/env. |

## Python stdlib — New stubs (v29)

| Module | Item | Behavior |
|--------|------|----------|
| pickle | dump, dumps, load, loads | Stub: dump no-op; dumps return b''; load/loads return None. Full impl requires serialization. |
| logging | getLogger, basicConfig, info, warning, error, debug | Stub: getLogger returns no-op logger; basicConfig no-op; level methods no-op. |

## Python stdlib — New stubs (v30)

| Module | Item | Behavior |
|--------|------|----------|
| multiprocessing | Process, Pool | Stub: Process start/join no-op; Pool returns None. Full impl requires process creation. |
| ssl | SSLContext, wrap_socket | Stub: wrap_socket returns None. Full impl requires TLS. |

## Python stdlib — New stubs

| Module | Item | Behavior |
|--------|------|----------|
| statistics | mean, median, stdev | Stub: return 0.0. |
| decimal | Decimal | Stub class; stores value; minimal __str__. |
| fractions | Fraction | Stub class; stores n/d; minimal __str__. |
