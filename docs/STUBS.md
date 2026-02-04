# protoPython Stub Status

This document catalogs stub implementations and their completion status.

## Native (C++) Stubs — Completed

| Component | Item | Status |
|-----------|------|--------|
| **set** | union, intersection, difference | Implemented |
| **itertools** | accumulate | Full implementation (default add, optional binary func) |
| **itertools** | groupby, product, combinations, combinations_with_replacement, permutations | Return empty iterator (no longer None) |
| **math** | isclose, log, log10, exp, sqrt, sin, cos | Implemented |
| **operator** | add, sub, mul, truediv, eq, lt, pow, floordiv, mod, neg, not_ | Implemented |

## Native (C++) Stubs — Remaining

| Component | Item | Notes |
|-----------|------|-------|
| **builtins** | help, memoryview | Stub retained. Implementation deferred: full impl needs pager (help) and buffer API (memoryview). Return None. |
| **builtins** | input, eval, exec | Stub retained. Implementation deferred: need stdin (input) and expression/statement parser (eval, exec). Return None/empty. |
| **builtins** | breakpoint, globals, locals | Stub retained. Implementation deferred: need debugger integration (breakpoint) and frame access (globals, locals). breakpoint no-op; globals/locals return empty dict. |

## Python stdlib Stubs — Enhanced

| Module | Item | Status |
|--------|------|--------|
| **os** | environ, getcwd, chdir, path, pathsep, sep | Stub values; no-op where applicable |
| **tempfile** | gettempdir, gettempprefix, mkstemp, TemporaryFile | mkstemp returns (0, path) instead of raising |
| **subprocess** | run, Popen, CompletedProcess | run returns CompletedProcess; Popen no-op |

## Python stdlib Stubs — Minimal (unchanged)

| Module | Item | Behavior |
|--------|------|----------|
| copy | copy, deepcopy | Return x unchanged |
| hashlib | md5, sha1, sha256 | Return None |
| base64 | b64encode, b64decode | Return empty bytes |
| struct | pack, unpack | Return empty bytes / empty tuple |
| getopt | getopt | Return [], [] |
| argparse | ArgumentParser | Stub parser |
| warnings | warn | No-op |
| time | time, sleep | Return 0.0; no-op |
| random | random, choice | Return 0.0; return first element |
| datetime | datetime | Stub class |
| uuid | uuid4 | Return fixed string |
| secrets | token_hex, token_urlsafe | Return placeholder |
| threading | Thread, Lock | Minimal stubs |
| enum | Enum | Minimal base class |
| functools | reduce, wraps, lru_cache | reduce implemented; wraps no-op; lru_cache stub (v20) |
| csv | reader, writer | Stub (NotImplementedError) — v21 |
| xml.etree | ElementTree, Element | Minimal stub — v21 |
