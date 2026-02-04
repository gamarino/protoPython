# protoPython Stub Status

This document catalogs stub implementations and their completion status.

## Native (C++) Stubs — Completed

| Component | Item | Status |
|-----------|------|--------|
| **set** | union, intersection, difference | Implemented |
| **itertools** | accumulate, groupby, product, combinations, combinations_with_replacement, permutations | Return empty iterator (no longer None) |
| **math** | isclose | Implemented |

## Native (C++) Stubs — Remaining

| Component | Item | Notes |
|-----------|------|-------|
| **builtins** | help, memoryview | Return None; full impl needs pager/buffer API |
| **builtins** | input, eval, exec | Return None/empty; need stdin/parser |
| **builtins** | breakpoint, globals, locals | Stubs for debugging/frame access |

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
| functools | reduce, wraps | reduce implemented; wraps no-op |
