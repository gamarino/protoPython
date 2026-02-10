# Stability and UX Implementation Plan (Next 100 Steps)

This plan covers the next 100 steps (v73–v77) focused on reaching a stable, feature-complete state for the `protoPython` language and REPL.

## Batch 1: Core Integrity & Frame Awareness (Steps 1385–1404)
**Goal**: Implement real frame-aware builtins and finalize process environment integration.

- [ ] **Step 1385-1388**: Implement `sys._getframe()` in `SysModule.cpp`.
- [ ] **Step 1389-1392**: Refactor `py_globals()` and `py_locals()` to use current frame state instead of stubs.
- [ ] **Step 1393-1396**: Implement `py_input()` with full stdin support.
- [ ] **Step 1397-1400**: Finalize `os.environ` synchronization with native `environ`.
- [ ] **Step 1401-1404**: Verification: `test_frames.py` and `test_environ_sync.py`.

## Batch 2: Concurrency & Identity (Steps 1405–1424)
**Goal**: Native threading support and thread-safe internal state.

- [ ] **Step 1405-1408**: Implement native `_thread` module (start_new_thread, allocate_lock).
- [ ] **Step 1409-1412**: Implement `threading.py` wrapper (Thread, Lock, RLock, Event).
- [ ] **Step 1413-1416**: Ensure `sys.modules` and `sys.path` are thread-safe or properly isolated.
- [ ] **Step 1417-1420**: Refine `id()` to ensure global uniqueness and stability.
- [ ] **Step 1421-1424**: Verification: `test_threading_native.py`.

## Batch 3: REPL & UX Perfection (Steps 1425–1444)
**Goal**: Professional developer experience in the interactive shell.

- [ ] **Step 1425-1428**: Implement REPL Multiline logic (block detection).
- [ ] **Step 1429-1432**: Add REPL History persistence (`~/.protopy_history`).
- [ ] **Step 1433-1436**: Implement REPL Autocomplete (basic identifier completion).
- [ ] **Step 1437-1440**: Integrate `sys.excepthook` for custom crash reporting.
- [ ] **Step 1441-1444**: Verification: Interactive REPL session testing.

## Batch 4: Advanced Language Features I (Steps 1445–1464)
**Goal**: Control flow primitives (yield and with).

- [ ] **Step 1445-1448**: Introduce `YIELD_VALUE` opcode and `Generator` object type.
- [ ] **Step 1449-1452**: Implement `yield` support in the parser and compiler.
- [ ] **Step 1453-1456**: Add `with` statement opcodes (`SETUP_WITH`, `WITH_CLEANUP_START/FINISH`).
- [ ] **Step 1457-1460**: Implement context manager protocol support in the compiler.
- [ ] **Step 1461-1464**: Verification: `test_yield.py` and `test_with.py`.

## Batch 5: Quality & Final Stability (Steps 1465–1484)
**Goal**: High-confidence release state.

- [ ] **Step 1465-1468**: Implement `sys.setrecursionlimit` and depth enforcement.
- [ ] **Step 1469-1472**: Perform a memory leak audit on core object creation paths.
- [ ] **Step 1473-1476**: Run full regression suite with 60s timeout enforcement.
- [ ] **Step 1477-1480**: Update all documentation to reflect v77 milestone status.
- [ ] **Step 1481-1484**: Final project-wide "Stable" smoke test.

## Testing & Verification
- All tests must be run with a **60s timeout**.
- Each batch must pass its specific functional tests before moving to the next.
- Commits will be made after each batch completion.
