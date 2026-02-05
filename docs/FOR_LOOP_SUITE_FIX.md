# For-Loop and Block Suite Parsing Fix

## Problem

Scripts with multiple statements inside `def`, `for`, or `if` bodies (e.g. `list_append_loop.py`, `str_concat_loop.py`, `range_iterate.py`) timed out or behaved incorrectly. The parser only accepted a **single statement** per block body: after `def main():` it parsed one statement (e.g. `lst = []`) and treated the rest of the function (the `for` loop and `return`) as **top-level** module code. That led to:

- The loop running in module scope with undefined names (e.g. `lst`), causing wrong behavior or extreme slowness.
- Benchmarks that should complete in seconds timing out after tens of seconds.

## Root Cause

1. **Parser**: `parseFunctionDef`, `parseFor`, and `parseIf` used `parseStatement()` once for the body, so only the first statement was attached to the block.
2. **No indentation tracking**: The tokenizer did not emit Indent/Dedent, so the parser had no way to know where a block ended.

## Solution

1. **Tokenizer**
   - Added `Indent` and `Dedent` token types.
   - After each newline, the tokenizer counts leading spaces/tabs and:
     - Emits `Indent` when the indent level increases (and pushes it onto a stack).
     - Emits `Dedent` when the indent level decreases (pops the stack).
     - Does nothing when the indent level is unchanged.
   - Comment-only and blank lines are handled so that the next line’s indent is still recognized.

2. **Parser**
   - Introduced **suite** parsing: a block body is either:
     - `Newline` + `Indent` + one or more statements + `Dedent`, or
     - a single statement on the same line (e.g. `def f(): pass`).
   - Added `SuiteNode` (a list of statements) and `parseSuite()` that returns it.
   - `FunctionDefNode`, `ForNode`, and `IfNode` (and `orelse` of `IfNode`) now use `parseSuite()` for their body instead of a single `parseStatement()`.

3. **Compiler**
   - Added `compileSuite(SuiteNode*)`: compiles each statement in order and emits `POP_TOP` after any statement that leaves a value on the stack (expression statements), except the last one.
   - Added `statementLeavesValue(ASTNode*)` to detect expression statements.
   - `compileNode()` dispatches on `SuiteNode` to `compileSuite()`.

## Files Changed

- `include/protoPython/Tokenizer.h`: `Indent`, `Dedent`; `atLineStart_`, `indentStack_`, `skipWhitespaceNoNewline()`.
- `src/library/Tokenizer.cpp`: Indent/dedent logic in `next()`, constructor initializes `indentStack_`.
- `include/protoPython/Parser.h`: `SuiteNode`, `parseSuite()` declaration.
- `src/library/Parser.cpp`: `parseSuite()` implementation; def/for/if/else use `parseSuite()` for body.
- `include/protoPython/Compiler.h`: `compileSuite()`, `statementLeavesValue()`.
- `src/library/Compiler.cpp`: `compileSuite()`, `statementLeavesValue()`, `compileNode()` handles `SuiteNode`.

## Verification

- `list_append_loop.py` with `BENCH_N=100` and `BENCH_N=500` completes in under a few seconds with protopy.
- All 59 execution engine tests pass (`test_execution_engine`).
- Foundation tests pass (`test_foundation`).
- Benchmark harness can use the default 60s timeout for loop benchmarks; no need for a reduced protopy timeout.

## Related

- Benchmarks: `benchmarks/list_append_loop.py`, `benchmarks/str_concat_loop.py`, `benchmarks/range_iterate.py`, `benchmarks/run_benchmarks.py`.
- Earlier workaround (short timeout for loop benchmarks) was removed after this fix.
