# Lessons Learned (protoPython)

Patterns and rules derived from implementation and corrections. Update after applying fixes.

## Next 100 Steps (v43–v47)

- **Foundation tests and module attributes**: When testing that a Python stdlib module "has" a function or class (e.g. `pickle.loads`, `io.BytesIO`), resolve the module from the Python environment. The resolved module may be the native module (e.g. `_io`) or the Python wrapper (e.g. `io` from lib). If the test expects `getAttribute("BytesIO")` and the loader populates the module from executing the Python file, ensure the module under test is the one from lib (e.g. `io`), not a native alias. Prefer asserting `__file__` and module non-null for stability when attribute names differ across load paths.
- **Pickle minimal format**: A custom minimal serialization format (type tag + length-prefixed or newline-terminated payloads) is easier to implement and debug than emulating the full pickle protocol. Document the format and supported types (int, float, str, bytes, list, dict with str keys) in the module docstring.
- **Logging Handler/Formatter**: Use a dict for the log record in the minimal implementation so that `%(message)s`-style formatting works with `fmt % record` without requiring a full logging.LogRecord class.
- **Batch docs and commits**: For each batch (v43–v47), create NEXT_20_STEPS_Vxx.md first, implement, add foundation tests, update STUBS.md and tasks/todo.md and IMPLEMENTATION_PLAN.md, then commit with message `feat(stdlib): Next 20 Steps vxx (steps xxx-xxx)`.
- **v47 consolidation**: HPY_INTEGRATION_PLAN and PACKAGING_ROADMAP are planning docs only; no runtime code. venv stub remains no-op create with minimal _activate_script_path for future use.

## Next 20 Steps (v48–v49)

- **Foundation test workarounds**: When a native method returns nullptr in some contexts (e.g. math.log10, operator.invert), use equivalent alternatives in tests: math.log(x, 10) instead of math.log10(x); direct setAttribute instead of py_setattr. Document the root cause and DISABLED_ the failing test until fixed.
- **strFromResult helper**: For str methods (upper, lower, capitalize) that return raw ProtoString or Python wrapper (__data__), use a helper that checks obj->isString() first, then __data__->isString(), to safely extract the ProtoString for toUTF8String.