# Execution Engine Regression Audit (Feb 2026)

This document records the resolution of critical regressions in the `protoPython` execution engine identified during the Phase 3 stabilization.

## Regressions and Fixes

### 1. Instruction Stepping Misalignment
**Problem**: Several opcodes (e.g., `LOAD_NAME`, `STORE_FAST`, `LOAD_ATTR`) were missing the `i++` step required to skip their instruction argument.
**Effect**: Non-deterministic execution where arguments were interpreted as opcodes.
**Fix**: Systematically restored local `i++` for all argument-consuming opcodes in `ExecutionEngine.cpp`.

### 2. Stack Overflow in Unit Tests
**Problem**: `GCStack` was modified to use automatic local slots. Unit tests using `executeBytecodeRange` manually often lacked these slots, leading to zero capacity.
**Effect**: "Python stack overflow" error message.
**Fix**: Implemented a `std::vector` fallback for `GCStack` when `nSlots` is zero or the context is not properly initialized for locals.

### 3. STORE_ATTR Stack Order
**Problem**: The `OP_STORE_ATTR` handler was popping the object and value in the wrong order.
**Effect**: Attributes were set on the value rather than the object, or incorrect object access.
**Fix**: Reordered pops to `const proto::ProtoObject* val = stack.back(); stack.pop_back(); const proto::ProtoObject* obj = stack.back(); stack.pop_back();`.

### 4. GET_ITER Fallback
**Problem**: `OP_GET_ITER` pushed the object itself if `__iter__` was missing.
**Effect**: Failure in `GetIterNoCrash` test which expected `None` or a null-like result for non-iterables in certain contexts.
**Fix**: Updated fallback to push `PROTO_NONE`.

### 5. compareOp Null Environment
**Problem**: Comparisons failed when `PythonEnvironment` was null (unit test scenario).
**Effect**: Unit tests for comparison opcodes failed.
**Fix**: Added a manual `a->compare(ctx, b)` fallback in `compareOp`.

## Verification Status
All 59 tests in `test_execution_engine` now pass.
Verification command: `./test/library/test_execution_engine --gtest_filter=ExecutionEngineTest.*`
