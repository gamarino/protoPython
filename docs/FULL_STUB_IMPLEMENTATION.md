# Full Stub Implementation Status

This document summarizes the phased implementation of stdlib stubs toward CPython-equivalent behavior. See [STUBS.md](STUBS.md) for the full catalog.

## Phase 0: Shared Infrastructure (C++)

- **Tokenizer** (`include/protoPython/Tokenizer.h`, `src/library/Tokenizer.cpp`): Minimal Python tokenizer; tokens for numbers, strings, identifiers, operators, newlines.
- **Parser** (`include/protoPython/Parser.h`, `src/library/Parser.cpp`): Recursive-descent parser; AST: Constant, Name, BinOp, Call, AttributeNode, SubscriptNode, SliceNode, ListLiteralNode, DictLiteralNode, TupleLiteralNode, AssignNode, ForNode, IfNode, GlobalNode, Module; statements (assignment, for, if, global, expression).
- **Compiler** (`include/protoPython/Compiler.h`, `src/library/Compiler.cpp`): Compiles AST to protoPython bytecode (full opcode set: LOAD/STORE_ATTR, BINARY/STORE_SUBSCR, BUILD_LIST/MAP/TUPLE/SLICE, GET_ITER, FOR_ITER, POP_JUMP_IF_FALSE, JUMP_ABSOLUTE, LOAD/STORE_GLOBAL, ROT_THREE, etc.); `makeCodeObject` / `runCodeObject`; forward-jump patching.
- **Builtins**: `compile(source, filename, mode)`, `eval(expr, globals, locals)`, `exec(source, globals, locals)`; internal `_tokenize_source(source)` for the tokenize module.

## Phase 1: Pure Python Stubs

- **binascii**: hex and base64 (already in lib).
- **uu**: uuencode/uudecode in `lib/python3.14/uu.py`.
- **quopri**: quoted-printable in `lib/python3.14/quopri.py`.
- **pprint**: recursive pformat, pprint, PrettyPrinter in `lib/python3.14/pprint.py`.
- **dis**: opname, disassemble, dis for protoPython code objects in `lib/python3.14/dis.py`.
- **random**, **statistics**, **urllib.parse**: Implemented or present in lib.

## Phase 2: Parser / Tokenizer / runpy

- **tokenize**: Uses `builtins._tokenize_source`; `generate_tokens`, `tokenize`, `untokenize` in `lib/python3.14/tokenize.py`.
- **runpy**: `run_path` (open, compile, exec); `run_module` via `__import__` in `lib/python3.14/runpy.py`.
- **ast**: Stub retained (parse/dump/literal_eval require C++ AST exposure).

## Phase 3: OS / Native

- **time**: Native `TimeModule` (`TimeModule.h`, `TimeModule.cpp`) with `time()` and `sleep(seconds)`; registered as module `time`.
- **os**, **subprocess**, **threading**, **atexit**, **tempfile**, **shutil**: Remain stubs; require further native hooks.

## Phase 4: External Libraries

- **hashlib**, **ssl**, **sqlite3**, **ctypes**: Remain stubs; require linking OpenSSL, SQLite, libffi and corresponding native modules.

## References

- [STUBS.md](STUBS.md) — Stub catalog and status.
- [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) — Roadmap and standard library note.
