# protopy Ground-Truth Audit (2026-04-30)

**Binary:** post-silent-halt-fix (HEAD `d9cb7b8be57f03ed62bf6715f8ac246f16812bda`; silent-halt fix landed in commit `efcfa7f3`).

**Audit run date:** 2026-04-30
**Audit script:** `tests/synthetic/sp_audit_truth.py` (committed alongside this report).

## Summary

| Category | Total | Real PASS | SILENT_HALT | FAIL_UNITTEST | CRASH | TIMEOUT | PARTIAL | UNKNOWN |
|---|---|---|---|---|---|---|---|---|
| Essential | 7 | 0 | 0 | 0 | 7 | 0 | 0 | 0 |
| Important | 6 | 0 | 0 | 0 | 6 | 0 | 0 | 0 |
| Necessary | 4 | 0 | 2 | 0 | 2 | 0 | 0 | 0 |
| Bootstrap | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| **Total** | **19** | **2** | **2** | **0** | **15** | **0** | **0** | **0** |


## Notable findings

**Previously-PASS tests no longer passing:**
- `test_json (package)` -- claimed PASS, now `CRASH` (exit 70)
- `test_decorator.py` -- claimed PASS, now `SILENT_HALT` (exit 0)
- `test_abc.py` -- claimed PASS, now `SILENT_HALT` (exit 0)
- `test_contextlib.py` -- claimed PASS, now `CRASH` (exit 70)
- `test_dataclasses.py` -- claimed PASS, now `CRASH` (exit 70)

**SILENT_HALT cases (cannot prove the module ran to the end):**
- `test_decorator.py` -- 2 non-empty stdout lines, no end-of-test marker.
- `test_abc.py` -- 0 non-empty stdout lines, no end-of-test marker.

**CRASH cases (15 of 19 tests):**
- missing module (ImportError) (10): `test_grammar.py`, `test_types.py`, `test_generators.py`, `test_base64.py`, `test_json (package)`, `test_sys.py`, `test_os.py`, `test_datetime.py`, `test_collections.py`, `test_functools.py`
- attribute lookup failure (5): `test_descr.py`, `test_asyncgen.py`, `test_re.py`, `test_contextlib.py`, `test_dataclasses.py`

**Surprises versus the prior CPYTHON_CONFORMANCE.md baseline:**

- The `test_descr.py` line said TIMEOUT (>5 min); it now CRASHes in ~3s on
  `ArgumentParser.conflict_handler` -- so the timeout was not the whole story.
  The descriptor performance work is no longer the proximate blocker; an
  attribute-resolution bug on the import path is.
- `test_grammar.py` was previously reported as 54/75 PASS. With the post-fix
  binary it cannot even reach `unittest.main` -- it crashes at import on
  `No module named 'typing'`.  The 54/75 number was therefore measured on a
  binary whose silent-halt behaviour was masking a runtime error during
  module import.  Whether the underlying interpreter still passes 54 of those
  cases is unknowable without first restoring the import chain.
- `test_json` was reported as 9/9 PASS.  The conformance line refers to
  `test_json.py` but the canonical entry point is `test_json/__main__.py`
  (a package).  Either way it crashes at import on `No module named 'doctest'`.
- The custom `test_decorator.py` and `test_abc.py` tests in `tests/` are
  **2-line and 4-line scripts** with no positive-confirmation output.  Their
  prior PASS verdict relied entirely on exit-code-0; the audit cannot prove
  they ran to the end.  This is independent of whether the interpreter is
  actually correct on those features (it might be) -- the tests themselves
  do not provide enough evidence.
- No TIMEOUTs, no PARTIALs, no UNKNOWNs.  Every test the catalog claimed is
  in one of three buckets: real PASS (Bootstrap, 2/19), insufficient evidence
  (SILENT_HALT, 2/19), or fails-on-import (CRASH, 15/19).

## Implications for SP planning

The crashes cluster cleanly into two orthogonal groups:

1. **Stdlib import surface (10 of 15 crashes).**  Tests die at module-import
   time on missing first-party modules: `typing`, `doctest`, `asyncio`
   (and its submodules `asyncio.graph`), `pdb`, `unittest.mock`,
   `test.support` and `test.support.os_helper` / `import_helper` /
   `socket_helper`.  These are not interpreter bugs -- they are stdlib
   completeness gaps on the protopy side.  Until the protopy stdlib provides
   these (or we mark them out-of-scope and patch the test files to skip),
   most of the Essential and Important rows cannot run at all.  Natural SP:
   "stdlib import-chain triage" -- audit which of these can be ported
   directly from CPython 3.14, which need protopy-specific stubs, and which
   must be vendored.

2. **Object-model attribute-resolution bugs (5 crashes).**  These are real
   interpreter regressions that surface even with the stdlib gaps:
   - `'ABCMeta' object has no attribute 'gen'` (test_asyncgen, test_contextlib)
   - `'ArgumentParser' object has no attribute 'conflict_handler'`
     (test_descr, test_re via argparse.py:1899)
   - `'Point' object has no attribute 'x'` (test_dataclasses)
   - `'socket' object has no attribute 'property has no setter'` (test_sys --
     this looks like a malformed error string, possibly a separate
     descriptor-formatting bug worth investigating on its own)
   - `reraise outside of except block` (test_base64 import chain) and
     `'NoneType' object is not callable` from typing.py:20 (test_functools,
     test_grammar via the typing import) -- these may be the same root cause.
   These are attribute-protocol bugs (likely affecting how protoPython
   resolves descriptors and class attributes during import) and are
   independent of the missing-stdlib problem; fixing them would not unblock
   any test on its own, but they will need attention before the import chain
   is healthy.

3. **Test hygiene (2 SILENT_HALTs).**  `tests/test_decorator.py` and
   `tests/test_abc.py` need end-of-test markers ("`<name> passed`" or an
   explicit assertion + print) before they can be classified as real PASSes.
   This is a tests-checked-into-the-repo fix, not an interpreter fix.

The 7 Essential tests in CPYTHON_CONFORMANCE.md should all be re-marked
**from PARTIAL/PASS to CRASH**; the 6 Important tests from UNBLOCKED to
CRASH; the 4 Necessary tests from PASS to either SILENT_HALT or CRASH.  The
Bootstrap row is the only one that stands.

## Detailed per-test results

### Essential / test_grammar.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_grammar.py`
- **Claimed status:** PARTIAL - 54/75 pass, 11 fail, 10 err, 0 crash (V154.8, 2026-04-25)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 2.58s

Last 5 stdout lines:
```
DUMP type.__init__: <object object at 0x744cf18657cc>
```

Last 10 stderr lines:
```
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/typinganndata/ann_module.py", line 7, in <module>
  File "<unknown>", in <module>
type: No module named 'typing'
protopy: unhandled exception in module execution (test_grammar):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_grammar.py", line 1, in <module>
  File "<unknown>", in <module>
type: No module named 'test.typinganndata.ann_module'
protopy: module 'test_grammar' exited with runtime error
```

### Essential / test_types.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_types.py`
- **Claimed status:** PARTIAL - 6/131 pass, runs to completion (V124, 2026-04-24)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 5.25s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/unittest/mock.py", line 7, in <module>
  File "<unknown>", in <module>
type: No module named 'asyncio'
protopy: unhandled exception in module execution (test_types):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_types.py", line 3, in <module>
  File "<unknown>", in <module>
type: No module named 'unittest.mock'
protopy: module 'test_types' exited with runtime error
```

### Essential / test_descr.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_descr.py`
- **Claimed status:** TIMEOUT - runs >5 min; type() descriptor tests expose slow paths
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 3.28s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/argparse.py", line 1899, in __init__
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/argparse.py", line 1899, in __init__
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
type: 'ArgumentParser' object has no attribute 'conflict_handler'
protopy: module 'test_descr' exited with runtime error
```

### Essential / test_generators.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_generators.py`
- **Claimed status:** PARTIAL - 0/1 pass (doctest runner fails); import chain runs
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 2.53s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/doctest.py", line 47, in <module>
  File "<unknown>", in <module>
type: No module named 'pdb'
protopy: unhandled exception in module execution (test_generators):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_generators.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'doctest'
protopy: module 'test_generators' exited with runtime error
```

### Essential / test_asyncgen.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_asyncgen.py`
- **Claimed status:** PARTIAL - 85 tests now run (0/85 pass, 80 errors, 5 failures); unblocked (V116)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 2.02s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
protopy: unhandled exception in module execution (test_asyncgen):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_asyncgen.py", line 2, in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
type: 'ABCMeta' object has no attribute 'gen'
protopy: module 'test_asyncgen' exited with runtime error
```

### Essential / test_base64.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_base64.py`
- **Claimed status:** PARTIAL - runs to completion, many failures (V110, 2026-04-23)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 1.59s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
type: reraise outside of except block
protopy: unhandled exception in module execution (test_base64):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_base64.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'test.support'
protopy: module 'test_base64' exited with runtime error
```

### Essential / test_json (package)

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_json/__main__.py`
- **Claimed status:** PASS - 9/9 tests pass (V124, 2026-04-24)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 2.25s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_json/__init__.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'doctest'
protopy: unhandled exception in module execution (__main__):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_json/__main__.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'test.test_json'
protopy: module '__main__' exited with runtime error
```

### Important / test_sys.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_sys.py`
- **Claimed status:** UNBLOCKED (V106)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 5.80s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
type: 'socket' object has no attribute 'property has no setter'
protopy: unhandled exception in module execution (test_sys):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_sys.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'test.support.socket_helper'
protopy: module 'test_sys' exited with runtime error
```

### Important / test_os.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_os.py`
- **Claimed status:** UNBLOCKED (V106)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 3.53s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/asyncio/__init__.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'asyncio.graph'
protopy: unhandled exception in module execution (test_os):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_os.py", line 6, in <module>
  File "<unknown>", in <module>
type: No module named 'asyncio'
protopy: module 'test_os' exited with runtime error
```

### Important / test_re.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_re.py`
- **Claimed status:** UNBLOCKED (V106)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 3.90s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/argparse.py", line 1899, in __init__
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/argparse.py", line 1899, in __init__
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
type: 'ArgumentParser' object has no attribute 'conflict_handler'
protopy: module 'test_re' exited with runtime error
```

### Important / test_datetime.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_datetime.py`
- **Claimed status:** UNBLOCKED (V106, requires frame introspection hardening)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 2.46s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/support/import_helper.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'test.support.os_helper'
protopy: unhandled exception in module execution (test_datetime):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_datetime.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'test.support.import_helper'
protopy: module 'test_datetime' exited with runtime error
```

### Important / test_collections.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_collections.py`
- **Claimed status:** UNBLOCKED (V106)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 3.10s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/doctest.py", line 47, in <module>
  File "<unknown>", in <module>
type: No module named 'pdb'
protopy: unhandled exception in module execution (test_collections):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_collections.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'doctest'
protopy: module 'test_collections' exited with runtime error
```

### Important / test_functools.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_functools.py`
- **Claimed status:** UNBLOCKED (V106)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 2.57s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/typing.py", line 20, in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
type: 'NoneType' object is not callable
protopy: unhandled exception in module execution (test_functools):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14/test/test_functools.py", line 2, in <module>
  File "<unknown>", in <module>
type: No module named 'typing'
protopy: module 'test_functools' exited with runtime error
```

### Necessary / test_decorator.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/tests/test_decorator.py`
- **Claimed status:** PASS (custom protoPython test - tests/test_decorator.py)
- **Actual status:** `SILENT_HALT`
- **Exit code:** 0
- **Wall-clock:** 0.02s

Last 5 stdout lines:
```
In dec, f: <function object at 0x71e23c30fe00>
foo is: <function object at 0x71e23c30fe00>
```

Last 10 stderr lines:
```
(empty)
```

### Necessary / test_abc.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/tests/test_abc.py`
- **Claimed status:** PASS (custom protoPython test - tests/test_abc.py)
- **Actual status:** `SILENT_HALT`
- **Exit code:** 0
- **Wall-clock:** 0.02s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
(empty)
```

### Necessary / test_contextlib.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/tests/test_contextlib.py`
- **Claimed status:** PASS (custom protoPython test - tests/test_contextlib.py)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 0.25s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
protopy: unhandled exception in module execution (test_contextlib):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/tests/test_contextlib.py", line 2, in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
  File "<unknown>", in <module>
type: 'ABCMeta' object has no attribute 'gen'
protopy: module 'test_contextlib' exited with runtime error
```

### Necessary / test_dataclasses.py

- **Path:** `/home/gamarino/Documentos/proyectos/protoPython/tests/test_dataclasses.py`
- **Claimed status:** PASS (custom protoPython test - tests/test_dataclasses.py)
- **Actual status:** `CRASH`
- **Exit code:** 70
- **Wall-clock:** 0.80s

Last 5 stdout lines:
```
(empty)
```

Last 10 stderr lines:
```
protopy: unhandled exception in module execution (test_dataclasses):
Traceback (most recent call last):
  File "/home/gamarino/Documentos/proyectos/protoPython/tests/test_dataclasses.py", line 2, in <module>
  File "<unknown>", in <module>
type: 'Point' object has no attribute 'x'
protopy: module 'test_dataclasses' exited with runtime error
```

### Bootstrap / import importlib

- **Path:** `<inline script>`
- **Claimed status:** PASS - import importlib works end-to-end (V101)
- **Actual status:** `PASS_UNITTEST`
- **Exit code:** 0
- **Wall-clock:** 0.10s

Last 5 stdout lines:
```
OK
```

### Bootstrap / import inspect

- **Path:** `<inline script>`
- **Claimed status:** PASS - import inspect works end-to-end (V101)
- **Actual status:** `PASS_UNITTEST`
- **Exit code:** 0
- **Wall-clock:** 0.68s

Last 5 stdout lines:
```
OK
```

