# protoPython Stub Status

This document catalogs stub implementations and their completion status.

## Native (C++) Stubs — Completed

| Component | Item | Status |
|-----------|------|--------|
| **set** | union, intersection, difference | Implemented |
| **itertools** | accumulate | Full implementation (default add, optional binary func) |
| **itertools** | groupby, product, combinations, combinations_with_replacement, permutations | Return empty iterator (no longer None) |
| **math** | isclose, log, log10, log2, log1p, exp, sqrt, sin, cos, tan, asin, acos, atan, atan2, degrees, radians, hypot, fmod, remainder, erf, erfc, gamma, lgamma, dist, perm, comb, factorial, prod, sumprod, isqrt, acosh, asinh, atanh, cosh, sinh, tanh, ulp, nextafter, ldexp, frexp, modf, cbrt, exp2, expm1, fma; constants pi, e, nan, inf | Implemented |
| **operator** | add, sub, mul, truediv, eq, lt, pow, floordiv, mod, neg, not_, invert, lshift, rshift, and_, or_, xor, index | Implemented |

## Native (C++) — Compiler / eval / exec (Phase 0)

| Component | Item | Status |
|-----------|------|--------|
| **builtins** | compile, eval, exec | Implemented via C++ tokenizer, parser, compiler; code object (co_consts, co_names, co_code); runCodeObject + executeMinimalBytecode. Expression and single-statement module supported. |

## Native (C++) Stubs — Remaining

| Component | Item | Notes |
|-----------|------|-------|
| **builtins** | help, memoryview | Stub retained. Implementation deferred: full impl needs pager (help) and buffer API (memoryview). Return None. |
| **builtins** | input | Stub retained. Implementation deferred: need stdin. |
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
| time | time, sleep | Native TimeModule: time() returns seconds since epoch (std::chrono); sleep(seconds) blocks. |
| random | random, choice, randint, getrandbits, seed | Implemented: LCG PRNG in lib/python3.14/random.py; deterministic when seeded. |
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
| atexit | register, unregister, _run_exitfuncs | Implemented (v37): register stores callbacks; _run_exitfuncs invoked at shutdown. |
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
| shutil | copyfile, rmtree, copy, move | copyfile and copy implemented (v37); rmtree, move stub. |
| mimetypes | guess_type, guess_extension, guess_all_extensions | Stub: return (None, None), None, []. |

## Python stdlib — New stubs (v24)

| Module | Item | Behavior |
|--------|------|----------|
| email | message_from_string | Stub: returns None. Full impl requires MIME parser. |
| runpy | run_path, run_module | run_path implemented: open path, compile source, exec in init_globals, return globals. run_module uses __import__ and runs __main__ if present. |

## Python stdlib — New stubs (v25)

| Module | Item | Behavior |
|--------|------|----------|
| urllib.parse | quote, unquote, urljoin | Stub: return string/url unchanged. Full impl requires percent-encoding. |
| zoneinfo | ZoneInfo | Stub class with key; __str__, __repr__. Full impl requires tzdata. |

## Python stdlib — New stubs (v26)

| Module | Item | Behavior |
|--------|------|----------|
| plistlib | load, loads, dump, dumps | Stub: load/loads return empty dict; dump/dumps no-op/empty bytes. Full impl requires plist parser. |
| quopri | encode, decode | Implemented: quoted-printable (RFC 2045) in lib/python3.14/quopri.py. |

## Python stdlib — New stubs (v27)

| Module | Item | Behavior |
|--------|------|----------|
| binascii | hexlify, unhexlify, b2a_base64, a2b_base64 | Implemented: hex and base64 (RFC 4648) in lib/python3.14/binascii.py. |
| uu | encode, decode | Implemented: uuencode/uudecode (3 bytes to 4 chars, line length 45) in lib/python3.14/uu.py. |

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

## Python stdlib — New stubs (v31)

| Module | Item | Behavior |
|--------|------|----------|
| shelve | open | Stub: returns dict-like with empty get/keys; __setitem__ no-op. Full impl requires dbm/serialization. |
| locale | getlocale, setlocale, getpreferredencoding, getdefaultlocale | Stub: getlocale/getdefaultlocale return (None,None); setlocale returns 'C'; getpreferredencoding returns 'UTF-8'. |

## Python stdlib — New stubs (v32)

| Module | Item | Behavior |
|--------|------|----------|
| ctypes | CDLL, c_int, c_double, c_long, byref, create_string_buffer, string_at | Stub: CDLL returns None; c_* return value; byref/string_at return None/b''. Full impl requires FFI. |
| sqlite3 | connect, Connection | Stub: connect returns Connection stub; execute/commit/close no-op. Full impl requires SQLite. |

## Python stdlib — New stubs (v33)

| Module | Item | Behavior |
|--------|------|----------|
| pprint | pprint, pformat, PrettyPrinter | Implemented: recursive pformat for dict, list, tuple; pprint to stream; PrettyPrinter class in lib/python3.14/pprint.py. |
| dis | dis, disassemble, distb, opname | Implemented: opname list for protoPython opcodes 100–155; disassemble(co) walks co_code and prints; dis(x) for code/function in lib/python3.14/dis.py. distb stub. |

## Python stdlib — New stubs (v34)

| Module | Item | Behavior |
|--------|------|----------|
| ast | parse, dump, literal_eval, walk, NodeVisitor | Stub: parse/dump return None/empty string; literal_eval return None; walk yields nothing; NodeVisitor no-op. Full impl requires C++ AST exposure. |
| tokenize | generate_tokens, tokenize, untokenize | Implemented: uses builtins._tokenize_source (native tokenizer); generate_tokens yields (type, value, start, end, line); untokenize reconstructs from token strings. |

## Python stdlib — New stubs (v35)

| Module | Item | Behavior |
|--------|------|----------|
| inspect | isfunction, ismodule, getsourcefile, signature | Stub: isfunction/ismodule return False; getsourcefile return None; signature raises ValueError. No real introspection. |
| types | FunctionType, ModuleType, SimpleNamespace | Stub: FunctionType, ModuleType placeholder classes; SimpleNamespace minimal (init with **kwargs, setattr). |

## Python stdlib — New stubs (v36)

Reserved for v36. No new stub entries in this batch (v36 focused on documentation: concurrency model, protoCore compatibility).

## Python stdlib — New stubs (v37)

| Module | Item | Behavior |
|--------|------|----------|
| atexit | _run_exitfuncs | Wired to PythonEnvironment::runExitHandlers at script/REPL shutdown. |
| shutil | copyfile, copy | Implemented via open/read/write. |
| traceback | extract_tb, format_list, format_exception | Implemented. |
| difflib | unified_diff | Implemented (line-by-line). |

## Python stdlib — New stubs (v39)

| Module | Item | Behavior |
|--------|------|----------|
| _os | environ_keys | Returns list of env var names (POSIX). |
| os | environ | Populated from _os.environ_keys + getenv when available. |
| argparse | ArgumentParser, add_argument, parse_args | Implemented (lib/python3.14/argparse.py). |
| configparser | ConfigParser: read, sections, get | Implemented (lib/python3.14/configparser.py). |
| getpass | getpass, getuser | getpass returns ""; getuser from os.environ. |

## Python stdlib — New stubs (v38)

| Module | Item | Behavior |
|--------|------|----------|
| statistics | mean, median, stdev, variance, pvariance | Implemented (lib/python3.14/statistics.py). Sample stdev/variance use n-1. |
| urllib.parse | quote, unquote, urljoin | Implemented (percent-encoding, lib/python3.14/urllib/parse.py). |
| dataclasses | dataclass | Enhanced: __init__ with default values from class attributes. |
| unittest | assertRaises, assertIn, main | Implemented: assertRaises context manager; main discovers and runs TestCase subclasses. |

## Python stdlib — New stubs

| Module | Item | Behavior |
|--------|------|----------|
| decimal | Decimal | Stub class; stores value; minimal __str__. Full impl in v40. |
| fractions | Fraction | Stub class; stores n/d; minimal __str__. Full impl in v40. |
