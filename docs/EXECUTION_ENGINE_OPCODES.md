# Execution Engine Opcodes

This is the reference for every opcode defined in
[`include/protoPython/ExecutionEngine.h`](../include/protoPython/ExecutionEngine.h):
114 opcodes with codes from 100 to 214. Code 167 is not used.

## Instruction format

The compiler (`src/library/Compiler.cpp`) stores a code object's instructions in
`co_code` as a flat sequence of integers. Every instruction occupies two slots: the
opcode and its argument, which is 0 when the opcode takes none. The argument is a full
integer; some opcodes pack several values into it. `TOS` below means the top of the
operand stack.

The names follow CPython, but the codes, argument encodings and semantics are
protoPython's own. The execution engine is implemented in
[`src/library/ExecutionEngine.cpp`](../src/library/ExecutionEngine.cpp).

## Fused opcodes

After jump targets are resolved, the compiler's peephole pass
(`Compiler::specialiseBytecode`) replaces three four-instruction sequences with the
fused opcodes 212-214. Each packs two operand indices, each at most 255, into the
argument. The other six slots of the replaced window are filled with `NOP`, so the
offsets of later instructions and all jump targets are unchanged. Setting the
environment variable `PROTOPY_NO_PEEPHOLE=1` disables the pass.

## Opcode table

"Not emitted by the compiler" marks opcodes that the engine handles but
`Compiler.cpp` never generates; "Reserved" marks opcodes that are only defined.

| Code | Opcode | Meaning |
|------|--------|---------|
| 100 | `OP_LOAD_CONST` | Push `constants[arg]`. |
| 101 | `OP_RETURN_VALUE` | Pop the top of the stack and return it. |
| 102 | `OP_LOAD_NAME` | Push the value bound to `names[arg]` in the current frame. |
| 103 | `OP_STORE_NAME` | Pop a value and bind it to `names[arg]` in the current frame. |
| 104 | `OP_BINARY_ADD` | Pop two values, push `left + right`. |
| 105 | `OP_BINARY_SUBTRACT` | Pop two values, push `left - right`. |
| 106 | `OP_CALL_FUNCTION` | Pop `arg` positional arguments and the callable, call it, push the result. |
| 107 | `OP_BINARY_MULTIPLY` | Pop two values, push `left * right`. |
| 108 | `OP_BINARY_TRUE_DIVIDE` | Pop two values, push `left / right`. |
| 109 | `OP_COMPARE_OP` | Pop two values, push the comparison result. `arg`: 0 `==`, 1 `!=`, 2 `<`, 3 `<=`, 4 `>`, 5 `>=`, 6 `in`, 7 `not in`, 8 `is`, 9 `is not`. |
| 110 | `OP_POP_JUMP_IF_FALSE` | Pop a value; if it is falsy, jump to bytecode index `arg`. |
| 111 | `OP_JUMP_ABSOLUTE` | Jump to bytecode index `arg`. |
| 112 | `OP_LOAD_ATTR` | Pop an object, push its attribute `names[arg]`. |
| 113 | `OP_STORE_ATTR` | Pop a value and an object, set the object's attribute `names[arg]`. |
| 114 | `OP_BUILD_LIST` | Pop `arg` values, push a list of them. |
| 115 | `OP_BINARY_SUBSCR` | Pop a key and a container, push `container[key]`. |
| 116 | `OP_BUILD_MAP` | Pop `arg` key/value pairs, push a dict. |
| 117 | `OP_STORE_SUBSCR` | Pop a value, a key and a container; set `container[key] = value`. |
| 118 | `OP_BUILD_TUPLE` | Pop `arg` values, push a tuple of them. |
| 119 | `OP_GET_ITER` | Replace the top of the stack with `iter(TOS)`. |
| 120 | `OP_FOR_ITER` | Advance the iterator on top of the stack: push the next value, or pop the iterator and jump to `arg` when it is exhausted. |
| 121 | `OP_UNPACK_SEQUENCE` | Pop a sequence and push its `arg` elements so that the first element ends on top. |
| 122 | `OP_LOAD_GLOBAL` | Push the global `names[arg]`. |
| 123 | `OP_STORE_GLOBAL` | Pop a value and bind the global `names[arg]`. |
| 124 | `OP_BUILD_SLICE` | Pop `arg` values (start, stop and optional step), push a slice. |
| 125 | `OP_ROT_TWO` | Swap the two top stack items. |
| 126 | `OP_DUP_TOP` | Duplicate the top of the stack. |
| 127 | `OP_BINARY_MODULO` | Pop two values, push `left % right`. |
| 128 | `OP_BINARY_POWER` | Pop two values, push `left ** right`. |
| 129 | `OP_BINARY_FLOOR_DIVIDE` | Pop two values, push `left // right`. |
| 130 | `OP_UNARY_NEGATIVE` | Replace the top of the stack with `-TOS`. |
| 131 | `OP_UNARY_NOT` | Replace the top of the stack with `not TOS`. |
| 132 | `OP_UNARY_INVERT` | Replace the top of the stack with `~TOS`. |
| 133 | `OP_POP_TOP` | Discard the top of the stack. |
| 134 | `OP_UNARY_POSITIVE` | Replace the top of the stack with `+TOS`. |
| 135 | `OP_NOP` | No operation. The peephole pass also uses it as padding. |
| 136 | `OP_INPLACE_ADD` | `a += b` (`__iadd__` when defined, otherwise `+`). |
| 137 | `OP_BINARY_LSHIFT` | Pop two values, push `left << right`. |
| 138 | `OP_BINARY_RSHIFT` | Pop two values, push `left >> right`. |
| 139 | `OP_INPLACE_SUBTRACT` | `a -= b` (`__isub__` when defined, otherwise `-`). |
| 140 | `OP_BINARY_AND` | Pop two values, push `left & right`. |
| 141 | `OP_BINARY_OR` | Pop two values, push `left \| right`. |
| 142 | `OP_BINARY_XOR` | Pop two values, push `left ^ right`. |
| 143 | `OP_INPLACE_MULTIPLY` | `a *= b` (`__imul__` when defined, otherwise `*`). |
| 144 | `OP_INPLACE_TRUE_DIVIDE` | `a /= b` (`__itruediv__` when defined, otherwise `/`). |
| 145 | `OP_INPLACE_FLOOR_DIVIDE` | `a //= b` (`__ifloordiv__` when defined, otherwise `//`). |
| 146 | `OP_INPLACE_MODULO` | `a %= b` (`__imod__` when defined, otherwise `%`). |
| 147 | `OP_INPLACE_POWER` | `a **= b` (`__ipow__` when defined, otherwise `**`). |
| 148 | `OP_INPLACE_LSHIFT` | `a <<= b` (`__ilshift__` when defined, otherwise `<<`). |
| 149 | `OP_INPLACE_RSHIFT` | `a >>= b` (`__irshift__` when defined, otherwise `>>`). |
| 150 | `OP_INPLACE_AND` | `a &= b` (`__iand__` when defined, otherwise `&`). |
| 151 | `OP_INPLACE_OR` | `a \|= b` (`__ior__` when defined, otherwise `\|`). |
| 152 | `OP_INPLACE_XOR` | `a ^= b` (`__ixor__` when defined, otherwise `^`). |
| 153 | `OP_ROT_THREE` | Move the top of the stack down to the third position: `... C B A` becomes `... A C B`. |
| 154 | `OP_ROT_FOUR` | Move the top of the stack down to the fourth position: `... D C B A` becomes `... A D C B`. Not emitted by the compiler. |
| 155 | `OP_DUP_TOP_TWO` | Duplicate the two top stack items: `... A B` becomes `... A B A B`. |
| 156 | `OP_BUILD_FUNCTION` | Pop a code object, push a function whose `__globals__` is the current frame. |
| 157 | `OP_LOAD_FAST` | Push local variable slot `arg`. |
| 158 | `OP_STORE_FAST` | Pop a value into local variable slot `arg`. |
| 159 | `OP_CALL_FUNCTION_KW` | Pop the tuple of keyword names, the keyword values, the positional arguments and the callable; call it and push the result. |
| 160 | `OP_BUILD_CLASS` | Pop the class body callable, the bases tuple and the name; build the class and push it. |
| 161 | `OP_DELETE_NAME` | Delete `names[arg]` from the current frame. |
| 162 | `OP_DELETE_ATTR` | Pop an object, delete its attribute `names[arg]`. |
| 163 | `OP_DELETE_SUBSCR` | Pop a key and a container, delete `container[key]`. |
| 164 | `OP_DELETE_GLOBAL` | Delete the global `names[arg]`. |
| 165 | `OP_DELETE_FAST` | Delete local variable slot `arg`. |
| 166 | `OP_RAISE_VARARGS` | Raise an exception; `arg` is the number of operands (0 or 1), and with 1 the exception is popped from the stack. |
| 168 | `OP_LIST_APPEND` | Pop a value and append it to the list `arg` positions down the stack (comprehensions). |
| 169 | `OP_MAP_ADD` | Pop a key (top) and a value and store them in the dict `arg + 1` positions down the stack (comprehensions). |
| 170 | `OP_SET_ADD` | Pop a value and add it to the set `arg` positions down the stack (comprehensions). |
| 171 | `OP_BUILD_SET` | Pop `arg` values, push a set of them. |
| 172 | `OP_YIELD_VALUE` | Pop a value and yield it from the generator; execution resumes at the next instruction. |
| 173 | `OP_SETUP_WITH` | Pop a context manager, push its `__exit__`, call `__enter__` and push the result; `arg` is the handler address used on an exception. |
| 174 | `OP_WITH_CLEANUP` | Pop an exception (or `None`) and the `__exit__` method, call `__exit__` with the exception's type and value and `None` (or three `None`s), and push its result as the suppression flag. |
| 175 | `OP_GET_YIELD_FROM_ITER` | Replace the top of the stack with the iterator used by `yield from`. |
| 176 | `OP_YIELD_FROM` | Send the value on top of the stack into the sub-iterator below it and yield what it produces; when it raises `StopIteration`, pop the sub-iterator and push the `StopIteration` value. |
| 177 | `OP_SETUP_FINALLY` | Push a block with handler address `arg` and the current stack depth; an exception jumps to the handler. |
| 178 | `OP_POP_BLOCK` | Pop the innermost block when leaving a `try`, `finally` or `with` body. |
| 179 | `OP_BUILD_STRING` | Pop `arg` values, concatenate them into one string, push it. |
| 180 | `OP_LOAD_DEREF` | Push a variable of an enclosing scope (closure). |
| 181 | `OP_STORE_DEREF` | Pop a value into a variable of an enclosing scope (closure). |
| 182 | `OP_CALL_FUNCTION_EX` | Pop a keyword mapping (when `arg & 1`), the positional-argument sequence and the callable; call it and push the result. |
| 183 | `OP_LIST_EXTEND` | Pop an iterable and extend the list below it with its items; the list stays on the stack. |
| 184 | `OP_DICT_UPDATE` | Pop a mapping and update the dict below it; the dict stays on the stack. |
| 185 | `OP_SET_UPDATE` | Pop an iterable and add its items to the set below it; the set stays on the stack. |
| 186 | `OP_LIST_TO_TUPLE` | Replace the list on top of the stack with a tuple of its items. |
| 187 | `OP_GET_AWAITABLE` | Replace the top of the stack with the iterator of an awaitable (`await`). |
| 188 | `OP_GET_AITER` | Replace the top of the stack with its asynchronous iterator (`async for`). |
| 189 | `OP_GET_ANEXT` | Push the awaitable for the next item of the asynchronous iterator on top of the stack (`async for`). |
| 190 | `OP_EXCEPTION_MATCH` | Pop an exception type and push whether the exception below it (which stays on the stack) is an instance of that type. |
| 191 | `OP_SETUP_ASYNC_WITH` | Pop an asynchronous context manager, push its `__aexit__` and the awaitable returned by `__aenter__`, and push a block with handler address `arg`. Not emitted by the compiler. |
| 192 | `OP_BINARY_MATRIX_MULTIPLY` | Pop two values, push `left @ right` (`__matmul__`, then `__rmatmul__`; `TypeError` if neither applies). |
| 193 | `OP_INPLACE_MATRIX_MULTIPLY` | `a @= b` (`__imatmul__` when defined, otherwise `@`). |
| 194 | `OP_RERAISE` | Re-raise the exception being handled (minimal implementation: the handler returns to the dispatch loop with the exception still pending). |
| 195 | `OP_JUMP_FORWARD` | Jump forward: the next instruction index plus `arg`. |
| 196 | `OP_FORMAT_VALUE` | Pop a value (and a format specification when `arg & 4`); push the value itself when `arg & 3` is 0, otherwise its `repr()`. Not emitted by the compiler. |
| 197 | `OP_GEN_START` | Reserved: defined, but not emitted by the compiler and not handled by the engine. |
| 198 | `OP_GET_LEN` | Reserved: defined, but not emitted by the compiler and not handled by the engine. |
| 199 | `OP_MATCH_MAPPING` | Reserved: defined, but not emitted by the compiler and not handled by the engine. |
| 200 | `OP_MATCH_SEQUENCE` | Reserved: defined, but not emitted by the compiler and not handled by the engine. |
| 201 | `OP_EXTENDED_ARG` | Reserved: defined, but not emitted by the compiler and not handled by the engine. |
| 202 | `OP_POP_EXCEPT` | Pop the active exception when leaving an `except` block. |
| 203 | `OP_IMPORT_STAR` | Pop a module and bind its attributes in the current frame (`from module import *`). |
| 204 | `OP_POP_JUMP_IF_TRUE` | Pop a value; if it is truthy, jump to bytecode index `arg`. |
| 205 | `OP_UNPACK_EX` | Pop a sequence and push the leading items, a list of the middle items and the trailing items; `arg` is `(count_after << 8) \| count_before`. |
| 206 | `OP_IMPORT_FROM` | With a module on top of the stack (it stays there), push its attribute `names[arg >> 1]`; raise `ImportError` if it does not exist. |
| 207 | `OP_PUSH_NULL` | Push a null marker used by call sequences. |
| 208 | `OP_BUILD_ANNOTATE` | Pop an `__annotations__` dict and push a PEP 649 `__annotate__` callable that returns it. |
| 209 | `OP_WRAP_RAW_LIST` | Pop a raw `ProtoList` and push a Python `list` that wraps it (end of a list comprehension). |
| 210 | `OP_BUILD_RAW_LIST` | Push an empty raw `ProtoList`, used as a list-comprehension accumulator. |
| 211 | `OP_DICT_MERGE` | Like `DICT_UPDATE`, but raise `TypeError` when a key is already present (class keywords and call keywords); the flag `DICT_MERGE_CALL_SITE` (0x100) in `arg` marks a call site. |
| 212 | `OP_ACC_FAST_FAST` | Fused `LOAD_FAST a; LOAD_FAST b; INPLACE_ADD; STORE_FAST a`; `arg` is `(a << 8) \| b`. |
| 213 | `OP_INC_FAST_K` | Fused `LOAD_FAST i; LOAD_CONST k; INPLACE_ADD; STORE_FAST i` for a small-integer constant `k`; `arg` is `(i << 8) \| k`. |
| 214 | `OP_LT_FAST_FAST_JF` | Fused `LOAD_FAST a; LOAD_FAST b; COMPARE_OP <; POP_JUMP_IF_FALSE target`; `arg` is `(a << 8) \| b`, and the jump target stays in the argument slot of the last replaced instruction. |

## Tests

The execution engine's unit tests are in
[`test/library/TestExecutionEngine.cpp`](../test/library/TestExecutionEngine.cpp):

```bash
ctest --test-dir build_release -R test_execution_engine --output-on-failure
```
