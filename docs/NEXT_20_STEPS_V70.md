# Next 20 Steps (v70)

These steps (1325–1344) focus on High-Quality Error Reporting and exception UX.

| Step | Status | Task | Summary |
|---|---|---|---|
| 1325 | [x] | Structured Formatting | Base exception formatter for the REPL implemented. |
| 1326 | [x] | Syntax Error Carets | Added line-pointer indicators (`^`) for syntax errors. |
| 1327 | [x] | ANSI Color Support | Implemented colorized error messages (Red/Cyan/Yellow). |
| 1328 | [x] | "Did you mean?" | Add suggestions for `NameError` using Levenshtein distance. |
| 1329 | [x] | Traceback Formatter | Implement a formatter that tracks REPL line numbers. |
| 1330 | [ ] | ProtoCore Translation | Map internal `protoCore` errors to Python exceptions. |
| 1331 | [ ] | KeyboardInterrupt UX | Refine the display of Ctrl-C interruptions. |
| 1332 | [ ] | SystemExit Handling | Gracefully report `SystemExit` with exit code. |
| 1333 | [ ] | Multi-line Error Context| Show context lines in tracebacks. |
| 1334 | [ ] | Exception Chaining | Support `raise ... from ...` display (visual). |
| 1335 | [x] | REPL State Sync | Ensure `sys.last_type/value/traceback` are set. |
| 1336 | [ ] | Color Theme Config | Basic support for disabling colors via `NO_COLOR`. |
| 1337 | [ ] | ImportError Hints | Add search path hints on certain `ImportError`s. |
| 1338 | [ ] | RecursionLimit UX | Better reporting for depth-exceeded errors. |
| 1339 | [x] | Attribute Suggestion | "Did you mean?" for `AttributeError`. |
| 1340 | [ ] | REPL Exception Hook | Integrate with `sys.excepthook`. |
| 1341 | [ ] | TODO Update | Sync `tasks/todo.md` with v70 progress. |
| 1342 | [ ] | Implementation Plan Sync | Update `docs/IMPLEMENTATION_PLAN.md`. |
| 1343 | [ ] | Walkthrough Update | Document the new error reporting UX. |
| 1344 | [ ] | Commit V70 | Document and finalize the v70 block. |

---
*Generated from the Master Plan v68-v72.*
