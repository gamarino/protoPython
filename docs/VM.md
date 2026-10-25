# The protoPython virtual machine

This document describes the bytecode virtual machine that runs protoPython
programs: how source becomes bytecode, how a frame is laid out, every opcode
of the instruction set, and worked examples. Every bytecode listing in it is
real output of `protopy --disasm`, checked by the ctest `docs_vm_examples`;
none is written by hand. It supersedes
[EXECUTION_ENGINE_OPCODES.md](EXECUTION_ENGINE_OPCODES.md).

Contents:

- [Start here](#start-here)
- [1. Overview](#1-overview)
- [2. Opcode reference](#2-opcode-reference)
- [3. Worked examples](#3-worked-examples)
- [4. How to add an opcode](#4-how-to-add-an-opcode)
- [5. How to add a primitive](#5-how-to-add-a-primitive)
- [6. Known oddities and issues](#6-known-oddities-and-issues)

The description is of `main` at commit `d453de62` (2026-10-25). Line numbers
are given as function names, which survive edits better.

## Start here

### Reading order

Read the source in the order a module travels through it:

| Step | Files | Key functions | What happens |
|---|---|---|---|
| 1. Driver | `src/runtime/main.cpp`, `src/library/PythonEnvironment.cpp` | `main`, `PythonEnvironment::executeModule`, `executeString` | `protopy script.py` creates a `PythonEnvironment` and imports the script as `__main__`. A module is parsed and compiled **whole**, then run. |
| 2. Tokenizer and parser | `src/library/Tokenizer.cpp`, `src/library/Parser.cpp`, `include/protoPython/Parser.h` | `Parser::parseModule` | Text becomes an AST (`ModuleNode`, `FunctionDefNode`, ...). There is no separate desugaring pass: decorators, comprehensions, `with`, `match` and f-strings are lowered by the compiler. |
| 3. Compiler | `src/library/Compiler.cpp`, `include/protoPython/Compiler.h` | `Compiler::compileModule`, `compileNode`, `compileFunctionDef`, `compileCall`, `compileTry`, `emitNameOp`, `applyPatches`, `specialiseBytecode` | One `Compiler` per scope (module, function, lambda, class body, comprehension). A nested scope is compiled by a child `Compiler` into its own code object, which becomes a constant of the enclosing one. |
| 4. Code objects | `include/protoPython/ExecutionEngine.h` (the opcodes), `Compiler.cpp` | `makeCodeObject`, `stampClosureLayout`, `stampArgCounts`, `stackEffect` | A code object is an ordinary protoCore object with attributes `co_code`, `co_consts`, `co_names`, `co_varnames`, ... (1.2). |
| 5. VM | `src/library/ExecutionEngine.cpp` | `runUserFunctionCall`, `runUserFunctionCallRaw`, `executeBytecodeRange`, `Activation::dispatch`, `invokeCallable`, `py_generator_resume`; `runCodeObject` in `Compiler.cpp` | The dispatch loop: one `ProtoContext` per activation of a code object. |
| 6. Built-ins and native modules | `src/library/BuiltinsModule.cpp`, `src/library/*Module.cpp`, `PythonEnvironment.cpp` | `py_len` and friends, `PythonEnvironment::initializeRootObjects` | Native methods (protoCore `ProtoMethod`s) installed as attributes of the `builtins` module, of native modules and of the type prototypes (`int`, `list`, `dict`, ...). |

`src/library/Disassembler.cpp` prints a code object; reading its `annotate`
alongside `ExecutionEngine.h` is a quick way to learn the operand of every
opcode.

Two other execution tiers exist and do not use this instruction set:
`protopyc` (`src/compiler/CppGenerator.cpp`) compiles Python source to C++
(docs/PROTOPYC_SPECIFICATION.md), and the HPy loader runs native extensions.
`BasicBlockAnalysis.cpp` and `ThreadingStrategy.cpp` (running basic blocks as
tasks, docs/REARCHITECTURE_PROTOCORE.md) are not called by the interpreter.

### Trying things with `--disasm`

```
protopy --disasm script.py
```

compiles the file exactly as a script is compiled (the peephole specialiser
included, 1.2) **without running it**, and prints the module's code object,
then every code object held in its constants, nested ones included, depth
first. Example 01 in section 3 shows the listing of `print(1 + 2)`; every
example there is such a listing.

How to read a listing:

- `code 0: <module> (file, line 1)` starts a code object. Its path names its
  place: `0` is the module, `0.1` the second code object among the module's
  constants, `0.1.0` the first code object among the constants of `0.1`.
  The constant that holds a code object shows the same path (`-> code 0.1`).
- `flags`: `co_flags` with the names of the bits that are set: `OPTIMIZED`
  (locals in slots), `NEWLOCALS`, `VARARGS`, `VARKEYWORDS`, `NESTED` (see 6),
  `GENERATOR`, `COROUTINE`. `parameters` is `co_nparams` (positional
  parameters), with `co_posonlyargcount` and `co_kwonlyargcount`.
  `generator` is printed when `co_is_generator` is set (generators and
  coroutines).
- `slots N`: `co_automatic_count`, the number of automatic slots of the
  activation's `ProtoContext`; `variables 0..k` are the slots of the local
  variables, cells and free variables, and the operand stack starts after
  them (`co_nlocalsplus`). See 1.3.
- `constants:` index, kind and value of `co_consts`. Kinds: `none` (protoCore's
  `PROTO_NONE`), `NoneType` (the environment's `None` object, see 6), `bool`,
  `int`, `float`, `str`, `bytes`, `tuple`, `code`, or the type name of any
  other object.
- `names:` `co_names`, used by the name and attribute opcodes.
- `variables (slot):` `co_varnames`: parameters first (positional,
  keyword-only, `*args`, `**kwargs`), then the other locals; the index is the
  slot. `free variables (slot):` `co_freevars`, in the slots after them.
  `cells:` the variables a nested scope captures, and the slot that holds
  each one's cell.
- `code:` one instruction per line: the source line (printed when it
  changes), the **offset** of the instruction in `co_code` (two words per
  instruction, so offsets are even), the opcode, the raw argument, and a
  comment that decodes the argument: the constant, the name, the slot's
  variable, a jump's absolute target (`to 28`), a call's argument count.

`protopy --disasm` exits with status 0, or 70 with a message when the file
does not parse or compile.

## 1. Overview

### 1.1 Pipeline

```
source --Parser::parseModule--> ModuleNode --Compiler::compileModule--> constants, names, code
  --makeCodeObject--> code object --runCodeObject--> executeBytecodeRange (module body)
```

A module (`PythonEnvironment::executeModule`, `exec`, `compile`) is parsed
and compiled in one piece; nothing runs until the whole module has compiled.
Each `def`, `lambda`, class body and comprehension is compiled by a child
`Compiler` into a code object that is stored as a **constant** of the
enclosing code object; at run time `LOAD_CONST` pushes it and
`BUILD_FUNCTION` makes a function of it.

Names are resolved at compile time into four kinds (`Compiler::emitNameOp`):

| Kind | Where | Opcodes |
|---|---|---|
| fast local | a function's own variable, in a slot | `LOAD_FAST` / `STORE_FAST` / `DELETE_FAST` |
| cell or free variable | captured by, or from, a nested scope | `LOAD_DEREF` / `STORE_DEREF` / `DELETE_DEREF`, `LOAD_CLASSDEREF` in a class body |
| global | declared `global`, or any unresolved name inside a function | `LOAD_GLOBAL` / `STORE_GLOBAL` / `DELETE_GLOBAL` |
| namespace name | module level, class bodies, and "mapped" functions | `LOAD_NAME` / `STORE_NAME` / `DELETE_NAME` |

A function is compiled **mapped** (not `CO_OPTIMIZED`: locals on the frame
object, `LOAD_NAME`/`STORE_NAME`) when its body calls `locals()`, `exec()` or
`eval()` (`Compiler::hasDynamicLocalsAccess`).

### 1.2 Instruction encoding and the code object

A code object is a protoCore object whose prototype is the environment's
`code` prototype. `makeCodeObject` and the `stamp*` helpers set:

| Attribute | Content |
|---|---|
| `co_code` | a tuple of integers: `op, arg, op, arg, ...` |
| `co_bytecode_native` | the same words as a C `int[]` in a pinned ByteBuffer; the VM dispatches on it |
| `co_consts`, `co_names` | constants (deduplicated by value for `int`, `float`, `str`, by identity otherwise); names (interned strings) |
| `co_varnames`, `co_nparams`, `co_kwonlyargcount`, `co_argcount`, `co_posonlyargcount`, `co_flags` | the frame layout and the binding rules |
| `co_freevars`, `co_cellvars`, `co_cellslots`, `co_nlocalsplus`, `co_selfref`, `co_selfref_cell` | the closure layout (1.4) |
| `co_automatic_count` | the activation's slot count (1.3) |
| `co_is_generator`, `co_name`, `co_filename`, `co_firstlineno`, `co_lnotab` | generator flag, names, the line table |

- **Instruction format.** Every instruction is two words: the opcode (100 to
  219) and its argument, a full `int` (0 when the opcode takes none). There
  is no `EXTENDED_ARG`: an argument never needs one.
- **Offsets and jumps.** An offset is the index of the opcode word in
  `co_code`, so instruction *k* is at offset *2k*. The jump arguments of
  `JUMP_ABSOLUTE`, `POP_JUMP_IF_FALSE`, `POP_JUMP_IF_TRUE`, `FOR_ITER`,
  `SETUP_FINALLY`, `SETUP_WITH` and `SETUP_ASYNC_WITH` are **absolute
  offsets**; `JUMP_FORWARD` is relative: `offset + 2 + arg`. The compiler
  emits forward jumps with argument 0 and records them; `applyPatches`
  writes `2 * target instruction index` into them when the scope is done.
- **Packed arguments.** The name opcodes carry `names index << 1`, with bit
  0 a flag: for `LOAD_NAME`, `LOAD_GLOBAL` "push a NULL marker first", for
  `LOAD_ATTR` "load as a method" (1.5). `BUILD_FUNCTION` packs flags and a
  cell count (`cells << 8 | 4 annotations | 2 keyword defaults | 1
  defaults`), `UNPACK_EX` `after << 8 | before`, `DICT_MERGE` a depth and the
  flag 0x100, the attribute write groups `descriptor << 5 | position`, the
  fused opcodes two byte-sized slot or constant indices.
- **Stack depth.** `Compiler::emit` keeps a running depth with `stackEffect`
  and records the maximum. The table is conservative (an unknown opcode
  counts 0; `LOAD_ATTR` counts +1 because of the method form) and the
  activation adds headroom (1.3).
- **Line table.** `co_lnotab` is a tuple of byte pairs (instruction delta,
  signed line delta) starting at `co_firstlineno`; the VM reads it to put a
  line on a traceback entry (`updateContextLocation`).
- **Peephole specialiser.** `Compiler::getBytecode` passes the finished code
  of every scope through `specialiseBytecode`, which replaces three
  four-instruction windows by one fused instruction followed by three `NOP`s
  (so no offset moves):

  | Window | Fused opcode |
  |---|---|
  | `LOAD_FAST a; LOAD_FAST b; INPLACE_ADD; STORE_FAST a` | `ACC_FAST_FAST (a << 8 \| b)` |
  | `LOAD_FAST i; LOAD_CONST k; INPLACE_ADD; STORE_FAST i` | `INC_FAST_K (i << 8 \| k)` |
  | `LOAD_FAST a; LOAD_FAST b; COMPARE_OP <; POP_JUMP_IF_FALSE t` | `LT_FAST_FAST_JF (a << 8 \| b)`, `t` kept in the argument of the third `NOP` |

  Every index must fit in a byte. `PROTOPY_NO_PEEPHOLE=1` in the environment
  disables the pass (read once per process). The pass does not check
  whether a jump lands inside a window; one does in practice, and the result
  is wrong (6, issue 1).
- **Attribute write groups.** `Compiler::tryCompileAttrGroup` compiles a run
  of 2 to 16 statements `p.name = value` on the same parameter `p`, whose
  values after the first cannot run code, to `STORE_ATTR_GROUP` ...
  `STORE_ATTR_GROUP_END` (2.6). `PROTOPY_ATTR_GROUPS=off` turns runs off.

### 1.3 Frames

Every activation of a code object runs in **its own `ProtoContext`**,
chained to the caller's context through `previous` (protoCore's
method-invocation model):

| Activation | Where the context is created |
|---|---|
| a Python function or lambda call | `runUserFunctionCall` and `runUserFunctionCallRaw` (`ExecutionEngine.cpp`): a `ContextScope` (`include/protoPython/MemoryManager.hpp`) holding the context and up to 256 slots on the C++ stack |
| each resumption of a generator or coroutine | `py_generator_resume`: a new `ContextScope`; the variables, operand stack and try levels are copied in from the generator object (`gi_locals`, `gi_stack`, saved blocks) and back out at the next yield |
| a module body, a class body, `exec`/`eval` of a code object | `runCodeObject` (`Compiler.cpp`): a heap-allocated `ProtoContext` |

The context's automatic slots hold the whole frame, so every value the frame
holds is in a slot the garbage collector traces:

```
slot 0 .. len(co_varnames)-1          parameters, then locals (a cell for a captured one)
slot .. co_nlocalsplus-1              free variables (the closure's cells); a class
                                      body's __class__ cell
slot co_nlocalsplus ..                operand stack
last 4 slots                          activation bookkeeping: the frame anchor
                                      (last), the activation marker, the handled
                                      exception, a synthesised traceback frame
                                      (PythonEnvironment::kActivationSlots)
```

`co_automatic_count` is `varnames + freevars + max stack + 128` for a
function and `max stack + 32` for a module (`compileFunctionDef`,
`PythonEnvironment::executeModule`). The operand stack (`GCStack`) is a view
of the slots above `co_nlocalsplus`; an overflow raises
`RuntimeError: evaluation stack overflow`. Only a context created elsewhere
with no slots at all gets a `std::vector` stack, which the collector does not
scan (`executeBytecodeRange`).

On entry `runUserFunctionCall`:

1. reads the function's `__fn_meta_cache__` (a ByteBuffer made by
   `BUILD_FUNCTION` with the code object, globals, counts and closure
   layout);
2. creates the context, marks the non-parameter locals unbound (a sentinel
   that `LOAD_FAST` reports as `UnboundLocalError`);
3. binds the arguments (`PythonEnvironment::bindArgumentsCore`: positional,
   keyword, defaults, `*args`, `**kwargs`, CPython's `TypeError`s);
4. makes the cells and copies the closure's cells into the free-variable
   slots (`setupClosureSlots`);
5. builds a frame object (`f_code`, `f_globals`, `f_back`) unless the
   function is `CO_OPTIMIZED`, not a generator and defines no inner function,
   in which case no frame object exists while it runs;
6. for a generator or coroutine, returns a generator object holding the
   frame state instead of running the body;
7. otherwise runs `executeBytecodeRange` from offset 0.

`runUserFunctionCallRaw` is the same for the common case (exactly the
positional parameters, no keywords, no generator, a cached function), taking
the arguments straight from the caller's operand stack.

`RETURN_VALUE` answers the top of the stack; the compiler ends every function
body with `LOAD_CONST None; RETURN_VALUE`. The result is anchored through
`ProtoContext::returnValue` while the context is destroyed. Python recursion
nests on the native stack; `RecursionScope` (entered for every call and every
`executeBytecodeRange`) raises `RecursionError` at the recursion limit.

### 1.4 Function values and closures

`BUILD_FUNCTION` (`createUserFunction`) builds a function object from a code
object: `__code__`, `__globals__` (the current globals), `__closure__` (a
`ProtoList` of cells, never a `ProtoTuple`, see `BUILD_FUNCTION_CELLS_SHIFT`),
`__defaults__`, `__kwdefaults__`, `__annotations__`, and the
`__fn_meta_cache__` ByteBuffer.

A variable some nested scope uses lives in a **cell**
(`PythonEnvironment::newCell`), so a nested function sees the current binding
and `nonlocal` writes reach the defining scope:

- the defining function keeps the cell in the variable's own slot
  (`co_cellslots`), and reads it with `LOAD_DEREF`/`STORE_DEREF`;
- `LOAD_CLOSURE slot` pushes the cell itself, and `BUILD_FUNCTION` with
  `n << 8` pops `n` cells into the new function's closure, in `co_freevars`
  order;
- the nested function finds each cell in a free-variable slot after its
  `co_varnames` (example 07).

A recursive nested `def` that is the only binding of its name binds the name
to itself at entry (`co_selfref`) instead of closing over the defining
scope's cell, which would close a cycle protoCore never collects
(`Compiler.h`, "Closures").

### 1.5 Calling conventions

The compiler follows CPython 3.11's layout. A call pushes a **NULL marker**
(or a receiver) and the callable, then the arguments:

| Shape | Stack before `CALL_FUNCTION n` | Emitted for |
|---|---|---|
| plain call | `NULL, f, a1 .. an` | `f(x)`: `PUSH_NULL` before a fast or deref load, or bit 0 of `LOAD_GLOBAL`/`LOAD_NAME` |
| method call | `func, self, a1 .. an` | `obj.m(x)`: `LOAD_ATTR` with bit 0 pushes the unbound function and the receiver when `m` is a plain Python function found on the class (or `__func__, __self__` of a method object that has them), otherwise `NULL, value` |
| legacy | `f, a1 .. an` | a few internal sequences (pattern matching, class bodies) |

`CALL_FUNCTION` does not know which shape it got: it treats the stack as
"modern" whenever there are at least `n + 2` values on it, and then reads the
value below the callable as a NULL marker or a receiver. A legacy call with
anything below it on the stack is misread (6, issue 3).

The callee is dispatched by `invokeCallable`:

1. a **native method** (`asMethod`, every built-in): called directly with
   the **caller's context**, after pinning the argument list
   (`TransientPin`), with the arguments as a `ProtoListArguments` view (1.5's
   context rule, section 5);
2. a **Python function** (`__code__` own attribute): `runUserFunctionCall`
   (the fast path `runUserFunctionCallRaw` is taken first, from
   `CALL_FUNCTION`, when the function has a meta cache);
3. a **Python class**: `runUserClassCall` (`__new__` and `__init__`);
4. anything else: `type(x).__call__`.

Other call opcodes:

- **`CALL_FUNCTION_KW n`**: the top of the stack is a constant tuple naming
  the last `len(names)` of the `n` arguments; they go to a keyword map.
- **`CALL_FUNCTION_EX flags`**: `f(*args, **kw)`; the positional arguments are
  one sequence (built with `BUILD_LIST`, `LIST_EXTEND`, `LIST_TO_TUPLE`) and,
  with `flags & 1`, one mapping (built with `BUILD_MAP 0`, `DICT_MERGE`).

### 1.6 GC safepoints and rooting

protoCore reclaims nothing a context has not handed over: a context's
allocations are chained on its young generation, which is submitted to the
collector when the context is destroyed or from `ProtoContext::safepoint()`.
The dispatch loop (`Activation::dispatch`) polls `ctx->safepoint()` **every
64 instructions** of an activation, at the top of the loop, after anchoring
the current frame object in its slot; pending Python signal handlers run at
the same point (but not while an attribute write group holds unpublished
writes). Allocation itself also polls for a stop-the-world inside protoCore.

Rooting inside the VM follows protoCore's EMBEDDER-CONFORMANCE rule 3 (no
`ProtoObject*` held only in a C++ local across an allocation or a
safepoint): operands stay in their stack slots until the result replaces
them (`BUILD_TUPLE`, `BUILD_MAP`, `BUILD_FUNCTION` push their partial result
as an extra slot while they build), and values that leave the stack while
native code runs are pinned (`TransientPin` around a native call's argument
list, around iterators in `UNPACK_SEQUENCE` and `LIST_EXTEND`). Native code
must follow docs/GC_BRIDGING.md.

### 1.7 Exceptions

A Python exception is a C++ exception (`PyException`, a
`proto::ProtoException`); there is no pending-exception flag. A Python `try`
is a C++ `try`:

- `SETUP_FINALLY handler` runs the protected range **as a nested dispatch**
  (`Activation::runRange`) inside a C++ `try`; `POP_BLOCK` ends the nested
  dispatch and the enclosing level continues after it.
- When a `ProtoException` escapes the range, the activation notes its
  traceback entry, cuts the operand stack back to the depth at
  `SETUP_FINALLY`, pushes `None, exc, exc` (for `SETUP_WITH` only `exc`),
  and runs the handler at `handler` **inside the C++ `catch`**
  (`runHandler`), so the exception is the active one (`sys.exc_info()`, bare
  `raise`).
- `POP_EXCEPT` ends the handler level; `RERAISE` rethrows the caught
  exception (`throw;`) when it runs in a handler level.
- A C++ standard exception thrown by native code is converted to
  `RuntimeError(what())` at the instruction that called it.

The layout `Compiler::compileTry` produces for `try/except/finally` (example
13 shows it in full):

```
      SETUP_FINALLY Lfinally         ; only with a finally clause
      SETUP_FINALLY Lexcept          ; only with except clauses
      <body>
      POP_BLOCK
      [POP_BLOCK, <finally body>]    ; a return inside the body
      POP_BLOCK
      JUMP_ABSOLUTE Lelse
Lexcept:                             ; stack: None, exc, exc
      <type> EXCEPTION_MATCH, POP_JUMP_IF_FALSE Lnext
      DUP_TOP, STORE_FAST e, POP_TOP x3
      SETUP_FINALLY Lcleanup         ; the clause body, then POP_BLOCK, POP_EXCEPT
      ...
Lnext:
      RERAISE 1                      ; no clause matched
Lelse:
      POP_BLOCK, <finally body>, JUMP_ABSOLUTE Lend
Lfinally:                            ; stack: None, exc, exc
      POP_TOP x3, SETUP_FINALLY L2, <finally body>, POP_BLOCK, RERAISE 1
L2:   RERAISE 2
Lend:
```

Generators save their try nesting (`Block`s) at a yield and rebuild it, one
nested dispatch per level, when resumed (`Activation::runLevel`).

## 2. Opcode reference

There are **119 opcodes**, values 100 to 219 with 167 unused
(`include/protoPython/ExecutionEngine.h`; the constant
`DICT_MERGE_CALL_SITE = 0x100` and `BUILD_FUNCTION_CELLS_SHIFT = 8` are
argument flags, not opcodes). The enum, the disassembler's name table
(`opcodeName`) and the VM's switch (`Activation::dispatch`) were compared
mechanically:

- the switch handles **114**; the other 5 (`GEN_START`, `GET_LEN`,
  `MATCH_MAPPING`, `MATCH_SEQUENCE`, `EXTENDED_ARG`) are reserved: defined,
  never emitted, and a no-op if executed (the switch's `default`);
- the compiler emits **111**: all but those 5 and `ROT_FOUR`,
  `FORMAT_VALUE`, `SETUP_ASYNC_WITH`, which the VM handles but nothing
  produces; `NOP`, `ACC_FAST_FAST`, `INC_FAST_K` and `LT_FAST_FAST_JF` are
  produced only by the peephole specialiser;
- `_opcode.opmap` (`OpcodeModule.cpp`) lacks 8 opcodes (`PUSH_NULL`,
  `BUILD_RAW_LIST`, `WRAP_RAW_LIST`, the three fused ones and the two write
  group ones) and names `LOAD_CLASSDEREF` `LOAD_FROM_DICT_OR_DEREF`; the
  standard library's `dis` module does not apply to protoPython code
  objects.

Notation: the stack effect is `before -> after`, top of stack on the right;
`NULL` is the call marker (a C++ `nullptr` in a stack slot). "Emitted by"
names the construct and the `Compiler` function. Errors are Python exceptions
with CPython's messages unless quoted.

Every handler checks its stack depth first and, on an underflow, skips the
instruction (or, in a few handlers, re-dispatches it, see 6); an underflow
cannot come from compiler output except through the bug in 6, issue 2.

### 2.1 Constants and the stack

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `LOAD_CONST` | 100 | constant index | `-> v` | Pushes `co_consts[arg]` (the very object; nothing is copied). | literals, code objects of nested scopes, `None` of an implicit return, keyword-name tuples, write group descriptors (`compileConstant` and many) |
| `POP_TOP` | 133 | 0 | `v ->` | Drops the top; a no-op on an empty stack. | expression statements (`emitStatementValueEnd`), handler clean-up, `with`, `match` |
| `ROT_TWO` | 125 | 0 | `a b -> b a` | | decorators, `with`, augmented subscripts, unwinding a `return` through `finally` |
| `ROT_THREE` | 153 | 0 | `a b c -> c a b` | Moves the top down to the third position. | augmented subscript assignment, chained comparisons, `async with` |
| `ROT_FOUR` | 154 | 0 | `a b c d -> d a b c` | | **never** (handled only) |
| `DUP_TOP` | 126 | 0 | `v -> v v` | | chained assignment, `except ... as`, chained comparisons, `:=`, `async with` |
| `DUP_TOP_TWO` | 155 | 0 | `a b -> a b a b` | | augmented subscript assignment (`compileAugAssign`) |
| `NOP` | 135 | 0, or a jump target | `->` | Does nothing. | only the peephole specialiser, as padding |
| `PUSH_NULL` | 207 | 0 | `-> NULL` | Pushes the call marker (1.5). | calls of fast, deref and attribute callables, decorators, comprehension and class calls (`emitNameOp`, `compileCall`) |

### 2.2 Local variables, cells and free variables

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `LOAD_FAST` | 157 | slot | `-> v` | Pushes the slot. `UnboundLocalError` when it holds the unbound sentinel (a local not yet assigned, or deleted by `DELETE_FAST`). | a fast local (`emitNameOp`); the self-reference of a recursive nested `def` |
| `STORE_FAST` | 158 | slot | `v ->` | Pops into the slot. | assignment to a fast local; comprehension targets |
| `DELETE_FAST` | 165 | slot | `->` | Unbinds the slot (`UnboundLocalError` if unbound). | `del x` of a fast local |
| `LOAD_DEREF` | 180 | slot holding a cell | `-> v` | Pushes the cell's contents; `NameError` / `UnboundLocalError` (`raiseEmptyCell`) when it is empty. | a cell or free variable (`emitNameOp`) |
| `STORE_DEREF` | 181 | slot holding a cell | `v ->` | Sets the cell's contents. | assignment to a cell or free variable (`nonlocal`) |
| `DELETE_DEREF` | 217 | slot holding a cell | `->` | Empties the cell (error when already empty). | `del x` of a cell or free variable |
| `LOAD_CLOSURE` | 218 | slot, or -1 | `-> cell` | Pushes the cell itself; -1 pushes a new empty cell (the compiler found no cell for the name). | before `BUILD_FUNCTION` of a nested scope with free variables (`emitClosureCells`, `emitLoadClosure`) |
| `LOAD_CLASSDEREF` | 219 | slot holding a cell | `-> v` | In a class body: the class namespace's binding of the free variable's name when it has one, else the cell's contents (CPython's `LOAD_FROM_DICT_OR_DEREF`). | a free variable read in a class body |

### 2.3 Namespace names and globals

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `LOAD_NAME` | 102 | `name << 1 \| push NULL` | `-> [NULL] v` | Looks the name up in the frame object (module, class namespace, mapped function's frame; a `__prepare__` namespace through `__getitem__`), then globals, then builtins (`resolveGlobalName`). `NameError` when absent. | names at module and class level, in mapped functions |
| `STORE_NAME` | 103 | `name << 1` | `v ->` | Binds the name in the frame object (through `__setitem__` for a custom class namespace), keeping its `__data__`/`__keys__` dict view in step; invalidates the global-resolution cache. | assignment at module and class level |
| `DELETE_NAME` | 161 | `name << 1` | `->` | Removes the binding (`__delitem__` for a custom namespace); `NameError` when unbound. | `del x` at module and class level |
| `LOAD_GLOBAL` | 122 | `name << 1 \| push NULL` | `-> [NULL] v` | Globals, then builtins, never the frame (`resolveGlobalName`, cached). `NameError` when absent. | unresolved names in functions; names declared `global`; internal calls of `__import__`, `isinstance`, ... |
| `STORE_GLOBAL` | 123 | `name << 1` | `v ->` | Binds the name in the current globals and updates the frame's `f_globals`. | assignment to a `global` name |
| `DELETE_GLOBAL` | 164 | `name << 1` | `->` | As `DELETE_NAME`, on the globals. | `del x` of a `global` name |
| `IMPORT_FROM` | 206 | `name << 1` | `mod -> mod v` | Pushes the module's attribute; `ImportError: cannot import name 'x' from 'm' (file)` when absent. | `from m import x` (`compileImportFrom`) |
| `IMPORT_STAR` | 203 | 0 | `mod ->` | Binds the names of `__all__` (any iterable), or every public name, in the frame. | `from m import *` |

`import m` is a call: `LOAD_GLOBAL __import__ (+ NULL)`, the arguments, and
`CALL_FUNCTION` (example 12).

### 2.4 Control flow and iteration

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `JUMP_ABSOLUTE` | 111 | absolute offset | `->` | | loops (back edge), `if`/`else`, `break`, `continue`, conditional expressions, `match` |
| `JUMP_FORWARD` | 195 | relative offset | `->` | Jumps to `offset + 2 + arg`. | only the asynchronous comprehension, where the argument is wrong (6, issue 6) |
| `POP_JUMP_IF_FALSE` | 110 | absolute offset | `v ->` | Pops; jumps when the value is falsy (`isTruthy`: `__bool__`, then `__len__`). | `if`, `while`, `and`, conditional expressions, comprehension filters, `except` clause tests, `match` |
| `POP_JUMP_IF_TRUE` | 204 | absolute offset | `v ->` | Pops; jumps when truthy. | `or`, `assert`, `with` (suppression flag), `match` |
| `GET_ITER` | 119 | 0 | `x -> it` | `iter(x)`; `TypeError: object is not iterable`. | `for`, comprehensions |
| `FOR_ITER` | 120 | absolute offset (loop exit) | `it -> it v` or `it ->` | Pushes the next value; when the iterator is exhausted pops it and jumps. Native iterators signal the end by returning nothing; no `StopIteration` object is built. | `for`, comprehensions, `async for` (on the `__next__` bridge `GET_AITER` installs) |

### 2.5 Calls, functions and classes

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `CALL_FUNCTION` | 106 | argc | `NULL\|func f\|self a1..an -> r` | Calls (1.5). Errors: whatever the callee raises; `'X' object is not callable`. | every call without keywords or unpacking (`compileCall`), decorators, `import` |
| `CALL_FUNCTION_KW` | 159 | argc, keywords included | `NULL f a1..an names -> r` | The constant tuple `names` names the last arguments. | calls with keyword arguments |
| `CALL_FUNCTION_EX` | 182 | 1 when a mapping is present | `NULL f args [kw] -> r` | `args` is a list or tuple (or any iterable); `kw` a dict whose keys must be strings (`TypeError: keywords must be strings`). | calls with `*args` or `**kwargs`; `async with`'s `__aexit__` |
| `RETURN_VALUE` | 101 | 0 | `v ->` | Ends the activation with the top (`None` on an empty stack). | `return`; the end of every body |
| `BUILD_FUNCTION` | 156 | `cells << 8 \| 4 annotations \| 2 kw defaults \| 1 defaults` | `c1..ck code [defaults] [kwdefaults] [annotations] -> fn` | Builds a function (1.4) with the current globals. Allocates; every operand stays in its slot until the function is complete. | `def`, `lambda`, class bodies, comprehensions, PEP 695 annotation scopes |
| `BUILD_CLASS` | 160 | 0 | `[NULL] name bases kwds body -> cls` | `PythonEnvironment::buildClass`: metaclass, `__prepare__`, runs the body function's code object on the namespace (`runCodeObject`, a new context), `__init_subclass__`. | `class` (`compileClassDef`); `kwds` is `None` or a dict |
| `BUILD_ANNOTATE` | 208 | 0 | `dict -> fn` | A native callable returning the `__annotations__` dict for any format (PEP 649, VALUE format only). | the end of a module body with annotations (`compileModule`) |

### 2.6 Attributes and subscripts

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `LOAD_ATTR` | 112 | `name << 1 \| method` | `obj -> v`, or with bit 0 `obj -> func self` / `obj -> NULL v` | The full attribute protocol (descriptors, MRO, `__getattribute__`, `__getattr__`) with an own-attribute fast path and a per-(type, name) inline cache. With bit 0, a plain Python function found on the class is pushed unbound with its receiver, so no bound method is allocated. `AttributeError`. | `obj.x`, `obj.m(...)` (`compileAttribute`) |
| `STORE_ATTR` | 113 | `name << 1` | `v obj ->` | `obj.x = v` (data descriptors, `__setattr__`, `__slots__`). | attribute assignment (`compileTarget`) |
| `DELETE_ATTR` | 162 | `name << 1` | `obj ->` | `del obj.x`. | `compileTarget` |
| `STORE_ATTR_GROUP` | 215 | `descriptor << 5 \| position` | `v obj -> v` (grouped) or `v obj ->` | A write of an attribute write group, not its last. At position 0 the VM decides once whether the run applies (an ordinary instance, no `__slots__`, no `__setattr__` or data descriptor of a written name on the type chain, the parameters the run reads bound). Grouped: keeps the value on the stack. Otherwise: exactly `STORE_ATTR`. | `tryCompileAttrGroup` |
| `STORE_ATTR_GROUP_END` | 216 | `descriptor << 5 \| position` | `v0 .. vn obj ->` | The last write: publishes every value of the run as **one** new version of the object (`ProtoObject::setAttributes`), which another thread sees whole or not at all; replaces the old object in the activation's slots. Otherwise `STORE_ATTR`. The descriptor constant is `(count, names index per write..., parameter slots...)`. | `tryCompileAttrGroup` |
| `BINARY_SUBSCR` | 115 | 0 | `c k -> c[k]` | Fast paths for list, tuple, dict and string; otherwise `__getitem__` (`__class_getitem__` for a class). | subscripts, augmented subscript assignment, `match` |
| `STORE_SUBSCR` | 117 | 0 | `v c k ->` | `c[k] = v`. | subscript assignment |
| `DELETE_SUBSCR` | 163 | 0 | `c k ->` | `del c[k]`. | `compileTarget` |
| `BUILD_SLICE` | 124 | 2 or 3 | `start stop [step] -> slice` | A `slice` object (step `None` when omitted). | `a[i:j:k]` |

### 2.7 Arithmetic, logic and comparison

The binary and in-place opcodes all have the stack effect `a b -> r`; the
unary ones `a -> r`. They are emitted by `compileBinOp`, `compileAugAssign`
and `compileUnaryOp` for the corresponding operator.

| Op | # | Operator | Fast path (no call, no allocation) |
|---|---|---|---|
| `BINARY_ADD` | 104 | `+` | both SmallIntegers, result in range |
| `BINARY_SUBTRACT` | 105 | `-` | same |
| `BINARY_MULTIPLY` | 107 | `*` | both SmallIntegers, product fits |
| `BINARY_TRUE_DIVIDE` | 108 | `/` | none |
| `BINARY_FLOOR_DIVIDE` | 129 | `//` | none |
| `BINARY_MODULO` | 127 | `%` | none |
| `BINARY_POWER` | 128 | `**` | none |
| `BINARY_MATRIX_MULTIPLY` | 192 | `@` | none (`__matmul__`, `__rmatmul__`, else `TypeError`) |
| `BINARY_LSHIFT` | 137 | `<<` | SmallIntegers, shift 0..62, result fits; `ValueError: negative shift count` |
| `BINARY_RSHIFT` | 138 | `>>` | SmallIntegers |
| `BINARY_AND` | 140 | `&` | SmallIntegers (booleans as integers) |
| `BINARY_OR` | 141 | `\|` | SmallIntegers; a class operand goes to `type.__or__` (PEP 604 unions) |
| `BINARY_XOR` | 142 | `^` | SmallIntegers |
| `INPLACE_ADD` | 136 | `+=` | SmallIntegers |
| `INPLACE_SUBTRACT` | 139 | `-=` | SmallIntegers |
| `INPLACE_MULTIPLY` | 143 | `*=` | none |
| `INPLACE_TRUE_DIVIDE` | 144 | `/=` | none |
| `INPLACE_FLOOR_DIVIDE` | 145 | `//=` | none |
| `INPLACE_MODULO` | 146 | `%=` | none |
| `INPLACE_POWER` | 147 | `**=` | none |
| `INPLACE_MATRIX_MULTIPLY` | 193 | `@=` | none |
| `INPLACE_LSHIFT` | 148 | `<<=` | see 6, issue 4 |
| `INPLACE_RSHIFT` | 149 | `>>=` | see 6, issue 4 |
| `INPLACE_AND` | 150 | `&=` | none |
| `INPLACE_OR` | 151 | `\|=` | none |
| `INPLACE_XOR` | 152 | `^=` | none |
| `UNARY_NEGATIVE` | 130 | `-x` | integers (bignum-safe), floats, booleans |
| `UNARY_POSITIVE` | 134 | `+x` | numbers (`+True` is `1`) |
| `UNARY_NOT` | 131 | `not x` | always `isTruthy`, no call for built-in values |
| `UNARY_INVERT` | 132 | `~x` | integers and booleans |
| `COMPARE_OP` | 109 | arg: 0 `==`, 1 `!=`, 2 `<`, 3 `<=`, 4 `>`, 5 `>=`, 6 `in`, 7 `not in`, 8 `is`, 9 `is not`; 10 (legacy exception match) is accepted, never emitted | 0..5 on two SmallIntegers |

Slow paths: the binary opcodes go through `binaryAdd`, `binarySubtract`, ...
and `binaryOpDispatch` (protoCore's promoting integer arithmetic for
`int`/`float`, then `__op__` / `__rop__` with `NotImplemented`, then
`TypeError: unsupported operand type(s)`). The in-place opcodes try
`__iop__` on the left operand first (unless it is an embedded value such as a
SmallInteger), then fall back to the binary operation. SmallIntegers are
protoCore's tagged 56-bit integers; leaving the range promotes to a
`LargeInteger`.

### 2.8 Exceptions and context managers

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `SETUP_FINALLY` | 177 | absolute offset of the handler | `->` | Runs what follows as a protected range (1.7). | `try`, the clean-up blocks of `except ... as`, `with`, `async with`, annotation blocks |
| `POP_BLOCK` | 178 | 0 | `->` | Ends the innermost protected range; the opening level continues after this instruction. | the end of every protected range |
| `POP_EXCEPT` | 202 | 0 | `->` | Ends the innermost handler level. | the end of an `except` clause, of `with` handlers |
| `EXCEPTION_MATCH` | 190 | 0 | `exc type -> exc bool` | `isinstance(exc, type)` for an exception class or a tuple of them (`isException`). | `except T:` (`compileTry`), `StopAsyncIteration` in async comprehensions |
| `RAISE_VARARGS` | 166 | 0, 1 or 2 | `[exc [cause]] ->` | 0: re-raise the active exception (`RuntimeError: No active exception to reraise`); 1: raise the top (a class is instantiated first), chaining `__context__`; 2: `raise exc from cause`. | `raise`, `assert` |
| `RERAISE` | 194 | 1 or 2 (not read) | `->` | Re-raises the exception being handled, without a new traceback entry. | the end of handlers |
| `SETUP_WITH` | 173 | absolute offset of the handler | `mgr -> exit value` | Looks `__enter__`/`__exit__` up on the type, pushes `__exit__`, calls `__enter__` and runs the rest as a protected range whose handler receives `exc` alone. | `with` (`compileWithItems`) |
| `WITH_CLEANUP` | 174 | 0 | `exit exc_or_None -> flag` | Calls `__exit__(type, exc, None)` or `__exit__(None, None, None)`; pushes the result (the suppression flag). | `with` |
| `SETUP_ASYNC_WITH` | 191 | absolute offset | `mgr -> aexit awaitable` | | **never** (`async with` compiles to `LOAD_ATTR __aenter__`, `GET_AWAITABLE`, `YIELD_FROM` and `SETUP_FINALLY`, example 19) |

### 2.9 Collections

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `BUILD_TUPLE` | 118 | count | `a1..an -> tuple` | A tuple backed by a `ProtoList` (never a `ProtoTuple`, which protoCore interns forever). | tuple displays, defaults, bases |
| `BUILD_LIST` | 114 | count | `a1..an -> list` | A `list` instance whose `__data__` is a `ProtoList`. | list displays, `*args` call sequences |
| `BUILD_SET` | 171 | count | `a1..an -> set` | `TypeError` for an unhashable item. | set displays, set comprehensions |
| `BUILD_MAP` | 116 | pair count | `k1 v1 .. kn vn -> dict` | A `dict` (`__data__` sparse list keyed by hash, `__keys__` in insertion order); keys hashed with the user's `__hash__`. | dict displays, keyword defaults, `**` call mappings |
| `BUILD_STRING` | 179 | count | `s1..sn -> str` | Joins the parts. | f-strings (`compileJoinedStr`) |
| `LIST_APPEND` | 168 | depth | `.. list .. v -> .. list ..` | Appends to the list `arg` slots below the value. | list comprehensions, list displays with `*` |
| `SET_ADD` | 170 | depth | `.. set .. v -> .. set ..` | | set comprehensions and displays |
| `MAP_ADD` | 169 | depth | `.. dict .. v k -> .. dict ..` | Value below the key. | dict comprehensions and displays |
| `LIST_EXTEND` | 183 | depth | `.. list .. it -> .. list ..` | Any iterable. | `[*a]`, `f(*args)` |
| `SET_UPDATE` | 185 | depth | `.. set .. it -> .. set ..` | | `{*a}` |
| `DICT_UPDATE` | 184 | depth | `.. dict .. m -> .. dict ..` | A dict, or any object with `keys` and `__getitem__`. | `{**m}` |
| `DICT_MERGE` | 211 | `depth \| 0x100 call site` | `.. dict .. m -> .. dict ..` | As `DICT_UPDATE`, but a key already present raises `TypeError: f() got multiple values for keyword argument 'k'` (naming `__build_class__()` for a class statement). | `f(**kw)`, class keywords |
| `LIST_TO_TUPLE` | 186 | 0 | `list -> tuple` | Shares the list's `ProtoList`. | `f(*args)`, tuple displays with `*` |
| `BUILD_RAW_LIST` | 210 | 0 | `-> raw` | Pushes a bare `ProtoList`, not a `list`. | the start of a list comprehension (`compileListComp`) |
| `WRAP_RAW_LIST` | 209 | 0 | `raw -> list` | Wraps the accumulator in a `list` instance. | the end of a list comprehension |
| `UNPACK_SEQUENCE` | 121 | count | `seq -> vn .. v1` | Iterates exactly `count` items; first item on top. `ValueError: not enough values to unpack (expected n, got m)` / `too many values to unpack (expected n)`. | `a, b = x`, `for a, b in ...` |
| `UNPACK_EX` | 205 | `after << 8 \| before` | `seq -> after.. list before..` | The middle items as a `list`. | `a, *b = x` (the starred target is mis-scoped, 6, issue 5) |

### 2.10 Generators and coroutines

| Op | # | Operand | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `YIELD_VALUE` | 172 | 0 | `v -> sent` | Suspends the generator with `v`: saves the resume offset, the operand stack and the try levels; the value passed to `send` is pushed on resumption. | `yield`, generator expressions |
| `GET_YIELD_FROM_ITER` | 175 | 0 | `x -> it` | `iter(x)` for `yield from`. | `yield from` |
| `YIELD_FROM` | 176 | 0 | `it sent -> it` (suspended) or `it sent -> result` | Sends `sent` into `it` (`send`, or `__next__` for a plain iterator; `gen.throw` is forwarded through `it.throw`); a yielded value suspends the generator **at this instruction**, so the next `send` re-runs it; `StopIteration` pops `it` and pushes its value. | `yield from`, `await` |
| `GET_AWAITABLE` | 187 | 0 | `x -> awaitable` | `x.__await__()`, or `x` itself without it. | `await`, `async with` |
| `GET_AITER` | 188 | 0 | `x -> aiter` | `x.__aiter__()`; for a class-defined async iterator installs a `__next__` bridge that drives `__anext__`, so `FOR_ITER` can run `async for`. | `async for`, async comprehensions |
| `GET_ANEXT` | 189 | 0 | `aiter -> aiter awaitable` | `aiter.__anext__()`. | async comprehensions only |

### 2.11 Fused opcodes (peephole specialiser)

Produced only by `Compiler::specialiseBytecode` (1.2), each followed by three
`NOP`s; the VM continues at `offset + 8`.

| Op | # | Operand | Replaces | Semantics |
|---|---|---|---|---|
| `ACC_FAST_FAST` | 212 | `a << 8 \| b` | `LOAD_FAST a; LOAD_FAST b; INPLACE_ADD; STORE_FAST a` | SmallInteger sum in place on the slots; otherwise checks both are bound and calls `binaryAdd` (so a list `a` is not extended in place: see 6). |
| `INC_FAST_K` | 213 | `i << 8 \| k` | `LOAD_FAST i; LOAD_CONST k; INPLACE_ADD; STORE_FAST i` | SmallInteger increment by a constant; otherwise `binaryAdd`. |
| `LT_FAST_FAST_JF` | 214 | `a << 8 \| b`; the jump target in the argument of the third `NOP` | `LOAD_FAST a; LOAD_FAST b; COMPARE_OP <; POP_JUMP_IF_FALSE t` | SmallInteger compare without touching the stack; otherwise `compareOp(<)`; jumps to `t` when false. |

### 2.12 Reserved and never emitted

| Op | # | Status |
|---|---|---|
| `GEN_START` | 197 | reserved: not emitted, not handled |
| `GET_LEN` | 198 | reserved |
| `MATCH_MAPPING` | 199 | reserved (`match` compiles to calls of `isinstance`, `len`, ...) |
| `MATCH_SEQUENCE` | 200 | reserved |
| `EXTENDED_ARG` | 201 | reserved (arguments are full integers) |
| `FORMAT_VALUE` | 196 | handled, never emitted: f-string conversions compile to calls of `repr`/`str`/`ascii` and `format`. Its handler pops the value before the format specification, the reverse of CPython's order. |
| `ROT_FOUR` | 154 | handled, never emitted |
| `SETUP_ASYNC_WITH` | 191 | handled, never emitted |

## 3. Worked examples

Each example is a file in `docs/vm-examples/`; the listing after it is that
file's golden output. Regenerate them with
`python3 docs/vm-examples/disasm_examples.py build_release/src/runtime/protopy --update`
after a compiler change, and review the notes. The module code object of most
examples only builds functions; its listing is still shown, because the
module is what `protopy` runs.

### Example 01: a constant and arithmetic, `print(1 + 2)`

<!-- vm-example: 01-arithmetic -->
```python
print(1 + 2)
```

```text
code 0: <module> (01-arithmetic.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 35: operand stack from 0
  constants:
    #0   int     1
    #1   int     2
    #2   NoneType None
  names:
    #0   print
  code:
     1     0  LOAD_NAME                     1  ; print (+ NULL)
           2  LOAD_CONST                    0  ; 1
           4  LOAD_CONST                    1  ; 2
           6  BINARY_ADD                    0
           8  CALL_FUNCTION                 1  ; 1 positional
          10  POP_TOP                       0
          12  LOAD_CONST                    2  ; None
          14  RETURN_VALUE                  0
```

- `0 LOAD_NAME 1`: the argument is `0 << 1 | 1`: name 0 (`print`), with a
  NULL marker pushed first, because the module level resolves names in its
  namespace (`LOAD_NAME`) and the result is called. Stack: `NULL print`.
- `2`-`4` push the constants; `6 BINARY_ADD` adds two SmallIntegers inline
  (no call, no allocation). There is no constant folding.
- `8 CALL_FUNCTION 1` calls `print` (a native method, in the module's
  context) with `[3]`; `10 POP_TOP` drops its result.
- `12`-`14`: every module ends with `return None`. Its `None` is the
  environment's `None` object (kind `NoneType`), not `PROTO_NONE` (6).
- `slots 35`: maximum stack 3 + 32 (1.3).

### Example 02: local variables in a function

<!-- vm-example: 02-locals -->
```python
def area(w, h):
    s = w * h
    return s
```

```text
code 0: <module> (02-locals.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code area, line 2>  -> code 0.0
    #1   NoneType None
  names:
    #0   area
  code:
     1     0  LOAD_CONST                    0  ; <code area, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; area
           6  LOAD_CONST                    1  ; None
           8  RETURN_VALUE                  0

code 0.0: area (02-locals.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 2 (0 positional-only, 0 keyword-only)
  slots 133: variables 0..2, operand stack from 3
  constants:
    #0   none    None
  variables (slot):
    #0   w
    #1   h
    #2   s
  code:
     2     0  LOAD_FAST                     0  ; w
           2  LOAD_FAST                     1  ; h
           4  BINARY_MULTIPLY               0
           6  STORE_FAST                    2  ; s
     3     8  LOAD_FAST                     2  ; s
          10  RETURN_VALUE                  0
          12  LOAD_CONST                    0  ; None
          14  RETURN_VALUE                  0
```

- The module builds the function: `LOAD_CONST` of the code object,
  `BUILD_FUNCTION 0` (no defaults, no closure), `STORE_NAME area`. The
  argument of `STORE_NAME` is `0 << 1`.
- In `area`, `w` and `h` are slots 0 and 1 (parameters first) and `s` slot 2;
  `LOAD_FAST`/`STORE_FAST` touch the slots directly.
- `8 LOAD_FAST s; 10 RETURN_VALUE` is the `return`; `12`-`14` is the
  implicit `return None` every function body ends with, here unreachable.
- `slots 133`: 3 variables + maximum stack 2 + 128.

### Example 03: `if` / `elif` and a conditional expression

<!-- vm-example: 03-conditionals -->
```python
def sign(n):
    if n < 0:
        return -1
    elif n == 0:
        return 0
    return 1


def parity(n):
    return "even" if n % 2 == 0 else "odd"
```

```text
code 0: <module> (03-conditionals.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code sign, line 2>  -> code 0.0
    #1   code    <code parity, line 10>  -> code 0.1
    #2   NoneType None
  names:
    #0   sign
    #1   parity
  code:
     1     0  LOAD_CONST                    0  ; <code sign, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; sign
     9     6  LOAD_CONST                    1  ; <code parity, line 10>
           8  BUILD_FUNCTION                0
          10  STORE_NAME                    2  ; parity
          12  LOAD_CONST                    2  ; None
          14  RETURN_VALUE                  0

code 0.0: sign (03-conditionals.py, line 2)
  flags 0x3 (OPTIMIZED | NEWLOCALS), parameters 1 (0 positional-only, 0 keyword-only)
  slots 131: variables 0..0, operand stack from 1
  constants:
    #0   int     0
    #1   int     1
    #2   none    None
  variables (slot):
    #0   n
  code:
     2     0  LOAD_FAST                     0  ; n
           2  LOAD_CONST                    0  ; 0
           4  COMPARE_OP                    2  ; <
           6  POP_JUMP_IF_FALSE            16  ; to 16
     3     8  LOAD_CONST                    1  ; 1
          10  UNARY_NEGATIVE                0
          12  RETURN_VALUE                  0
          14  JUMP_ABSOLUTE                28  ; to 28
     4    16  LOAD_FAST                     0  ; n
          18  LOAD_CONST                    0  ; 0
          20  COMPARE_OP                    0  ; ==
          22  POP_JUMP_IF_FALSE            28  ; to 28
     5    24  LOAD_CONST                    0  ; 0
          26  RETURN_VALUE                  0
     6    28  LOAD_CONST                    1  ; 1
          30  RETURN_VALUE                  0
          32  LOAD_CONST                    2  ; None
          34  RETURN_VALUE                  0

code 0.1: parity (03-conditionals.py, line 10)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 131: variables 0..0, operand stack from 1
  constants:
    #0   int     2
    #1   int     0
    #2   str     'even'
    #3   str     'odd'
    #4   none    None
  variables (slot):
    #0   n
  code:
    10     0  LOAD_FAST                     0  ; n
           2  LOAD_CONST                    0  ; 2
           4  BINARY_MODULO                 0
           6  LOAD_CONST                    1  ; 0
           8  COMPARE_OP                    0  ; ==
          10  POP_JUMP_IF_FALSE            16  ; to 16
          12  LOAD_CONST                    2  ; 'even'
          14  JUMP_ABSOLUTE                18  ; to 18
          16  LOAD_CONST                    3  ; 'odd'
          18  RETURN_VALUE                  0
          20  LOAD_CONST                    4  ; None
          22  RETURN_VALUE                  0
```

- `sign`: `n < 0` is `LOAD_FAST`, `LOAD_CONST`, `COMPARE_OP 2` (`<`);
  `POP_JUMP_IF_FALSE 16` jumps to the `elif` test at offset 16 (absolute).
  `14 JUMP_ABSOLUTE 28` (over the `elif` branch) follows a `return` and is
  never reached.
- The peephole specialiser leaves `n < 0` alone: its window would need two
  `LOAD_FAST`s.
- `parity`: the conditional expression is `POP_JUMP_IF_FALSE` to the `else`
  value and `JUMP_ABSOLUTE` over it (`compileCondExpr`).
- `sign` lacks the `NESTED` flag that `parity` has (6).

### Example 04: define and call a function

<!-- vm-example: 04-def-call -->
```python
def greet(name):
    return "hello " + name


print(greet("world"))
```

```text
code 0: <module> (04-def-call.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 35: operand stack from 0
  constants:
    #0   code    <code greet, line 2>  -> code 0.0
    #1   str     'world'
    #2   NoneType None
  names:
    #0   greet
    #1   print
  code:
     1     0  LOAD_CONST                    0  ; <code greet, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; greet
     5     6  LOAD_NAME                     3  ; print (+ NULL)
           8  LOAD_NAME                     1  ; greet (+ NULL)
          10  LOAD_CONST                    1  ; 'world'
          12  CALL_FUNCTION                 1  ; 1 positional
          14  CALL_FUNCTION                 1  ; 1 positional
          16  POP_TOP                       0
          18  LOAD_CONST                    2  ; None
          20  RETURN_VALUE                  0

code 0.0: greet (04-def-call.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 131: variables 0..0, operand stack from 1
  constants:
    #0   str     'hello '
    #1   none    None
  variables (slot):
    #0   name
  code:
     2     0  LOAD_CONST                    0  ; 'hello '
           2  LOAD_FAST                     0  ; name
           4  BINARY_ADD                    0
           6  RETURN_VALUE                  0
           8  LOAD_CONST                    1  ; None
          10  RETURN_VALUE                  0
```

- `6`-`14`: `print(greet("world"))`: two NULL-marker calls nest; the inner
  call's result is the outer call's argument.
- At run time `CALL_FUNCTION 1` on `greet` takes the fast path
  (`runUserFunctionCallRaw`): one new `ProtoContext`, `name` bound to slot 0
  straight from the caller's stack.
- `BINARY_ADD` on two strings takes the slow path (`binaryAdd`, string
  concatenation).

### Example 05: recursion

<!-- vm-example: 05-fib -->
```python
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


print(fib(20))
```

```text
code 0: <module> (05-fib.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 35: operand stack from 0
  constants:
    #0   code    <code fib, line 2>  -> code 0.0
    #1   int     20
    #2   NoneType None
  names:
    #0   fib
    #1   print
  code:
     1     0  LOAD_CONST                    0  ; <code fib, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; fib
     7     6  LOAD_NAME                     3  ; print (+ NULL)
           8  LOAD_NAME                     1  ; fib (+ NULL)
          10  LOAD_CONST                    1  ; 20
          12  CALL_FUNCTION                 1  ; 1 positional
          14  CALL_FUNCTION                 1  ; 1 positional
          16  POP_TOP                       0
          18  LOAD_CONST                    2  ; None
          20  RETURN_VALUE                  0

code 0.0: fib (05-fib.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 132: variables 0..0, operand stack from 1
  constants:
    #0   int     2
    #1   int     1
    #2   none    None
  names:
    #0   fib
  variables (slot):
    #0   n
  code:
     2     0  LOAD_FAST                     0  ; n
           2  LOAD_CONST                    0  ; 2
           4  COMPARE_OP                    2  ; <
           6  POP_JUMP_IF_FALSE            12  ; to 12
     3     8  LOAD_FAST                     0  ; n
          10  RETURN_VALUE                  0
     4    12  LOAD_GLOBAL                   1  ; fib (+ NULL)
          14  LOAD_FAST                     0  ; n
          16  LOAD_CONST                    1  ; 1
          18  BINARY_SUBTRACT               0
          20  CALL_FUNCTION                 1  ; 1 positional
          22  LOAD_GLOBAL                   1  ; fib (+ NULL)
          24  LOAD_FAST                     0  ; n
          26  LOAD_CONST                    0  ; 2
          28  BINARY_SUBTRACT               0
          30  CALL_FUNCTION                 1  ; 1 positional
          32  BINARY_ADD                    0
          34  RETURN_VALUE                  0
          36  LOAD_CONST                    2  ; None
          38  RETURN_VALUE                  0
```

- Inside `fib`, `fib` is a global (`LOAD_GLOBAL 1`: name 0, NULL marker);
  `LOAD_GLOBAL` resolves through the environment's cache.
- Each recursive call is `CALL_FUNCTION 1` on a user function: one
  `ProtoContext` per activation, nested on the native stack.
- No frame object is built for `fib` (it is `CO_OPTIMIZED`, not a generator
  and defines no function, 1.3).

### Example 06: loops and the fused opcodes

<!-- vm-example: 06-loops -->
```python
def total(n):
    s = 0
    i = 0
    while i < n:
        s += i
        i += 1
    return s


def sum_squares(xs):
    out = 0
    for x in xs:
        out += x * x
    return out
```

```text
code 0: <module> (06-loops.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code total, line 2>  -> code 0.0
    #1   code    <code sum_squares, line 11>  -> code 0.1
    #2   NoneType None
  names:
    #0   total
    #1   sum_squares
  code:
     1     0  LOAD_CONST                    0  ; <code total, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; total
    10     6  LOAD_CONST                    1  ; <code sum_squares, line 11>
           8  BUILD_FUNCTION                0
          10  STORE_NAME                    2  ; sum_squares
          12  LOAD_CONST                    2  ; None
          14  RETURN_VALUE                  0

code 0.0: total (06-loops.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 133: variables 0..2, operand stack from 3
  constants:
    #0   int     0
    #1   int     1
    #2   none    None
  variables (slot):
    #0   n
    #1   i
    #2   s
  code:
     2     0  LOAD_CONST                    0  ; 0
           2  STORE_FAST                    2  ; s
     3     4  LOAD_CONST                    0  ; 0
           6  STORE_FAST                    1  ; i
     4     8  LT_FAST_FAST_JF             256  ; if not i < n: to 34
          10  NOP                           0
          12  NOP                           0
          14  NOP                          34
     5    16  ACC_FAST_FAST               513  ; s += i
          18  NOP                           0
          20  NOP                           0
          22  NOP                           0
     6    24  INC_FAST_K                  257  ; i += 1
          26  NOP                           0
          28  NOP                           0
          30  NOP                           0
          32  JUMP_ABSOLUTE                 8  ; to 8
     7    34  LOAD_FAST                     2  ; s
          36  RETURN_VALUE                  0
          38  LOAD_CONST                    2  ; None
          40  RETURN_VALUE                  0

code 0.1: sum_squares (06-loops.py, line 11)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 135: variables 0..2, operand stack from 3
  constants:
    #0   int     0
    #1   none    None
  variables (slot):
    #0   xs
    #1   x
    #2   out
  code:
    11     0  LOAD_CONST                    0  ; 0
           2  STORE_FAST                    2  ; out
    12     4  LOAD_FAST                     0  ; xs
           6  GET_ITER                      0
           8  FOR_ITER                     26  ; to 26
          10  STORE_FAST                    1  ; x
    13    12  LOAD_FAST                     2  ; out
          14  LOAD_FAST                     1  ; x
          16  LOAD_FAST                     1  ; x
          18  BINARY_MULTIPLY               0
          20  INPLACE_ADD                   0
          22  STORE_FAST                    2  ; out
          24  JUMP_ABSOLUTE                 8  ; to 8
    14    26  LOAD_FAST                     2  ; out
          28  RETURN_VALUE                  0
          30  LOAD_CONST                    1  ; None
          32  RETURN_VALUE                  0
```

- `total`: `while i < n` became `8 LT_FAST_FAST_JF 256` (`1 << 8 | 0`: slots
  `i` and `n`); its jump target, 34, is the argument of the `NOP` at 14.
  `s += i` became `ACC_FAST_FAST 513` (`2 << 8 | 1`) and `i += 1`
  `INC_FAST_K 257` (slot 1, constant 1). The back edge `32 JUMP_ABSOLUTE 8`
  lands on the fused compare.
- `sum_squares`: `for x in xs` is `GET_ITER` then `FOR_ITER 26`, which jumps
  to the loop exit when the iterator is exhausted, popping it. The iterator
  stays on the stack during the body (6, issue 2).
- `out += x * x` is not fused: `BINARY_MULTIPLY` sits inside the window.

### Example 07: closures, `nonlocal` and `lambda`

<!-- vm-example: 07-closures -->
```python
def make_adder(n):
    def add(x):
        return x + n
    return add


def counter():
    count = 0
    def bump():
        nonlocal count
        count += 1
        return count
    return bump


square = lambda x: x * x
```

```text
code 0: <module> (07-closures.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code make_adder, line 2>  -> code 0.0
    #1   code    <code counter, line 8>  -> code 0.1
    #2   code    <code <lambda>, line 17>  -> code 0.2
    #3   NoneType None
  names:
    #0   make_adder
    #1   counter
    #2   square
  code:
     1     0  LOAD_CONST                    0  ; <code make_adder, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; make_adder
     7     6  LOAD_CONST                    1  ; <code counter, line 8>
           8  BUILD_FUNCTION                0
          10  STORE_NAME                    2  ; counter
    16    12  LOAD_CONST                    2  ; <code <lambda>, line 17>
          14  BUILD_FUNCTION                0
          16  STORE_NAME                    4  ; square
          18  LOAD_CONST                    3  ; None
          20  RETURN_VALUE                  0

code 0.0: make_adder (07-closures.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 132: variables 0..1, operand stack from 2
  constants:
    #0   code    <code add, line 3>  -> code 0.0.0
    #1   none    None
  variables (slot):
    #0   n
    #1   add
  cells:
    n in slot 0
  code:
     2     0  LOAD_CLOSURE                  0  ; cell n
           2  LOAD_CONST                    0  ; <code add, line 3>
           4  BUILD_FUNCTION              256  ; 1 closure cell
           6  STORE_FAST                    1  ; add
     4     8  LOAD_FAST                     1  ; add
          10  RETURN_VALUE                  0
          12  LOAD_CONST                    1  ; None
          14  RETURN_VALUE                  0

code 0.0.0: add (07-closures.py, line 3)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 132: variables 0..1, operand stack from 2
  constants:
    #0   none    None
  variables (slot):
    #0   x
  free variables (slot):
    #1   n
  code:
     3     0  LOAD_FAST                     0  ; x
           2  LOAD_DEREF                    1  ; free n
           4  BINARY_ADD                    0
           6  RETURN_VALUE                  0
           8  LOAD_CONST                    0  ; None
          10  RETURN_VALUE                  0

code 0.1: counter (07-closures.py, line 8)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 0 (0 positional-only, 0 keyword-only)
  slots 132: variables 0..1, operand stack from 2
  constants:
    #0   int     0
    #1   code    <code bump, line 10>  -> code 0.1.0
    #2   none    None
  variables (slot):
    #0   bump
    #1   count
  cells:
    count in slot 1
  code:
     8     0  LOAD_CONST                    0  ; 0
           2  STORE_DEREF                   1  ; cell count
     9     4  LOAD_CLOSURE                  1  ; cell count
           6  LOAD_CONST                    1  ; <code bump, line 10>
           8  BUILD_FUNCTION              256  ; 1 closure cell
          10  STORE_FAST                    0  ; bump
    13    12  LOAD_FAST                     0  ; bump
          14  RETURN_VALUE                  0
          16  LOAD_CONST                    2  ; None
          18  RETURN_VALUE                  0

code 0.1.0: bump (07-closures.py, line 10)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 0 (0 positional-only, 0 keyword-only)
  slots 130: variables 0..0, operand stack from 1
  constants:
    #0   int     1
    #1   none    None
  free variables (slot):
    #0   count
  code:
    10     0  POP_TOP                       0
    11     2  LOAD_DEREF                    0  ; free count
           4  LOAD_CONST                    0  ; 1
           6  INPLACE_ADD                   0
           8  STORE_DEREF                   0  ; free count
    12    10  LOAD_DEREF                    0  ; free count
          12  RETURN_VALUE                  0
          14  LOAD_CONST                    1  ; None
          16  RETURN_VALUE                  0

code 0.2: <lambda> (07-closures.py, line 17)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 131: variables 0..0, operand stack from 1
  variables (slot):
    #0   x
  code:
    16     0  LOAD_FAST                     0  ; x
           2  LOAD_FAST                     0  ; x
           4  BINARY_MULTIPLY               0
           6  RETURN_VALUE                  0
```

- `make_adder`: `n` is captured, so slot 0 holds a cell (`cells: n in slot
  0`). `LOAD_CLOSURE 0` pushes the cell and `BUILD_FUNCTION 256` (`1 << 8`)
  pops it into `add`'s closure.
- `add`: `n` is a free variable in slot 1, after `x`; `LOAD_DEREF 1` reads
  the cell's current contents.
- `counter`: `count = 0` is `STORE_DEREF 1` into the cell; `bump` updates it
  with `LOAD_DEREF`, `INPLACE_ADD`, `STORE_DEREF`.
- `bump` starts with `POP_TOP` on an empty stack: the `nonlocal` statement
  is followed by a stray `POP_TOP` (6, issue 2). Here it is harmless.
- The `lambda` body ends at its `RETURN_VALUE`; there is no implicit
  `return None`. Its code object says line 17, its instructions line 16 (6).

### Example 08: default and keyword-only arguments

<!-- vm-example: 08-defaults -->
```python
def connect(host, port=80, *, timeout=10):
    return (host, port, timeout)


connect("example.org", timeout=5)
```

```text
code 0: <module> (08-defaults.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 38: operand stack from 0
  constants:
    #0   code    <code connect, line 2>  -> code 0.0
    #1   int     80
    #2   str     'timeout'
    #3   int     10
    #4   str     'example.org'
    #5   int     5
    #6   tuple   ('timeout',)
    #7   NoneType None
  names:
    #0   connect
  code:
     1     0  LOAD_CONST                    0  ; <code connect, line 2>
           2  LOAD_CONST                    1  ; 80
           4  BUILD_TUPLE                   1
           6  LOAD_CONST                    2  ; 'timeout'
           8  LOAD_CONST                    3  ; 10
          10  BUILD_MAP                     1
          12  BUILD_FUNCTION                3  ; defaults, keyword defaults
          14  STORE_NAME                    0  ; connect
     5    16  LOAD_NAME                     1  ; connect (+ NULL)
          18  LOAD_CONST                    4  ; 'example.org'
          20  LOAD_CONST                    5  ; 5
          22  LOAD_CONST                    6  ; ('timeout',)
          24  CALL_FUNCTION_KW              2  ; 2 arguments, the last ones named by the tuple on top
          26  POP_TOP                       0
          28  LOAD_CONST                    7  ; None
          30  RETURN_VALUE                  0

code 0.0: connect (08-defaults.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 2 (0 positional-only, 1 keyword-only)
  slots 134: variables 0..2, operand stack from 3
  constants:
    #0   none    None
  variables (slot):
    #0   host
    #1   port
    #2   timeout
  code:
     2     0  LOAD_FAST                     0  ; host
           2  LOAD_FAST                     1  ; port
           4  LOAD_FAST                     2  ; timeout
           6  BUILD_TUPLE                   3
           8  RETURN_VALUE                  0
          10  LOAD_CONST                    0  ; None
          12  RETURN_VALUE                  0
```

- The defaults are evaluated when the function is defined: `(80,)` with
  `BUILD_TUPLE 1` and `{'timeout': 10}` with `BUILD_MAP 1`, then
  `BUILD_FUNCTION 3` (flags 1 and 2).
- `connect("example.org", timeout=5)` is `CALL_FUNCTION_KW 2`: two
  arguments, the last named by the constant tuple `('timeout',)` on top.
  Binding (`bindArgumentsCore`) puts `port`'s default in slot 1 and
  `timeout` in slot 2.

### Example 09: unpacking, `**kwargs` and argument spreading

<!-- vm-example: 09-unpacking -->
```python
def unpack(pair, rest):
    a, b = pair
    first, *others = rest
    return a, b, first, others


def options(**kwargs):
    return kwargs


def forward(f, args, kw):
    return f(*args, **kw)
```

```text
code 0: <module> (09-unpacking.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code unpack, line 2>  -> code 0.0
    #1   code    <code options, line 8>  -> code 0.1
    #2   code    <code forward, line 12>  -> code 0.2
    #3   NoneType None
  names:
    #0   unpack
    #1   options
    #2   forward
  code:
     1     0  LOAD_CONST                    0  ; <code unpack, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; unpack
     7     6  LOAD_CONST                    1  ; <code options, line 8>
           8  BUILD_FUNCTION                0
          10  STORE_NAME                    2  ; options
    11    12  LOAD_CONST                    2  ; <code forward, line 12>
          14  BUILD_FUNCTION                0
          16  STORE_NAME                    4  ; forward
          18  LOAD_CONST                    3  ; None
          20  RETURN_VALUE                  0

code 0.0: unpack (09-unpacking.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 2 (0 positional-only, 0 keyword-only)
  slots 136: variables 0..4, operand stack from 5
  constants:
    #0   none    None
  names:
    #0   others
  variables (slot):
    #0   pair
    #1   rest
    #2   first
    #3   b
    #4   a
  code:
     2     0  LOAD_FAST                     0  ; pair
           2  UNPACK_SEQUENCE               2
           4  STORE_FAST                    4  ; a
           6  STORE_FAST                    3  ; b
     3     8  LOAD_FAST                     1  ; rest
          10  UNPACK_EX                     1  ; 1 before, 0 after
          12  STORE_FAST                    2  ; first
          14  STORE_GLOBAL                  0  ; others
     4    16  LOAD_FAST                     4  ; a
          18  LOAD_FAST                     3  ; b
          20  LOAD_FAST                     2  ; first
          22  LOAD_GLOBAL                   0  ; others
          24  BUILD_TUPLE                   4
          26  RETURN_VALUE                  0
          28  LOAD_CONST                    0  ; None
          30  RETURN_VALUE                  0

code 0.1: options (09-unpacking.py, line 8)
  flags 0x1b (OPTIMIZED | NEWLOCALS | VARKEYWORDS | NESTED), parameters 0 (0 positional-only, 0 keyword-only)
  slots 130: variables 0..0, operand stack from 1
  constants:
    #0   none    None
  variables (slot):
    #0   kwargs
  code:
     8     0  LOAD_FAST                     0  ; kwargs
           2  RETURN_VALUE                  0
           4  LOAD_CONST                    0  ; None
           6  RETURN_VALUE                  0

code 0.2: forward (09-unpacking.py, line 12)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 3 (0 positional-only, 0 keyword-only)
  slots 136: variables 0..2, operand stack from 3
  constants:
    #0   none    None
  variables (slot):
    #0   f
    #1   args
    #2   kw
  code:
    12     0  PUSH_NULL                     0
           2  LOAD_FAST                     0  ; f
           4  BUILD_LIST                    0
           6  LOAD_FAST                     1  ; args
           8  LIST_EXTEND                   1
          10  LIST_TO_TUPLE                 0
          12  BUILD_MAP                     0
          14  LOAD_FAST                     2  ; kw
          16  DICT_MERGE                  257  ; dict at depth 1, call site
          18  CALL_FUNCTION_EX              1  ; positional sequence and keyword mapping
          20  RETURN_VALUE                  0
          22  LOAD_CONST                    0  ; None
          24  RETURN_VALUE                  0
```

- `a, b = pair`: `UNPACK_SEQUENCE 2` pushes the items with the first on top,
  so the first `STORE_FAST` stores `a`.
- `first, *others = rest`: `UNPACK_EX 1` (one before, none after) pushes the
  middle `list` below `first`. **`others` is stored with `STORE_GLOBAL`**:
  the compiler did not treat the starred target as a local (6, issue 5).
- `options(**kwargs)` binds the keyword dictionary to slot 0 (`VARKEYWORDS`).
- `f(*args, **kw)`: the positional arguments become a tuple (`BUILD_LIST 0`,
  `LIST_EXTEND 1`, `LIST_TO_TUPLE`), the keywords a fresh dict (`BUILD_MAP 0`,
  `DICT_MERGE 257` = depth 1 and the call-site flag), then
  `CALL_FUNCTION_EX 1`.

### Example 10: keyed and attribute lookup

<!-- vm-example: 10-keyed-lookup -->
```python
def lookup(d, obj):
    d["k"] = obj.attr
    return d["k"], obj.method(1)
```

```text
code 0: <module> (10-keyed-lookup.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code lookup, line 2>  -> code 0.0
    #1   NoneType None
  names:
    #0   lookup
  code:
     1     0  LOAD_CONST                    0  ; <code lookup, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; lookup
           6  LOAD_CONST                    1  ; None
           8  RETURN_VALUE                  0

code 0.0: lookup (10-keyed-lookup.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 2 (0 positional-only, 0 keyword-only)
  slots 135: variables 0..1, operand stack from 2
  constants:
    #0   str     'k'
    #1   int     1
    #2   none    None
  names:
    #0   attr
    #1   method
  variables (slot):
    #0   d
    #1   obj
  code:
     2     0  LOAD_FAST                     1  ; obj
           2  LOAD_ATTR                     0  ; attr
           4  LOAD_FAST                     0  ; d
           6  LOAD_CONST                    0  ; 'k'
           8  STORE_SUBSCR                  0
     3    10  LOAD_FAST                     0  ; d
          12  LOAD_CONST                    0  ; 'k'
          14  BINARY_SUBSCR                 0
          16  LOAD_FAST                     1  ; obj
          18  LOAD_ATTR                     3  ; method (method)
          20  LOAD_CONST                    1  ; 1
          22  CALL_FUNCTION                 1  ; 1 positional
          24  BUILD_TUPLE                   2
          26  RETURN_VALUE                  0
          28  LOAD_CONST                    2  ; None
          30  RETURN_VALUE                  0
```

- `d["k"] = obj.attr`: the value first (`LOAD_ATTR 0`, plain read), then the
  container and the key, then `STORE_SUBSCR`.
- `d["k"]` is `BINARY_SUBSCR` (the dict fast path).
- `obj.method(1)` is `LOAD_ATTR 3` (`1 << 1 | 1`): method form, pushing the
  unbound function and `obj` when `method` is a Python function of `obj`'s
  class, so `CALL_FUNCTION 1` passes `obj` as the first argument with no
  bound-method object.

### Example 11: a built-in call

<!-- vm-example: 11-builtin-call -->
```python
def size(xs):
    return len(xs)
```

```text
code 0: <module> (11-builtin-call.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code size, line 2>  -> code 0.0
    #1   NoneType None
  names:
    #0   size
  code:
     1     0  LOAD_CONST                    0  ; <code size, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; size
           6  LOAD_CONST                    1  ; None
           8  RETURN_VALUE                  0

code 0.0: size (11-builtin-call.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 131: variables 0..0, operand stack from 1
  constants:
    #0   none    None
  names:
    #0   len
  variables (slot):
    #0   xs
  code:
     2     0  LOAD_GLOBAL                   1  ; len (+ NULL)
           2  LOAD_FAST                     0  ; xs
           4  CALL_FUNCTION                 1  ; 1 positional
           6  RETURN_VALUE                  0
           8  LOAD_CONST                    0  ; None
          10  RETURN_VALUE                  0
```

- `len` is a global of the function (`LOAD_GLOBAL`), resolved to the
  `builtins` module's native method `py_len`.
- `CALL_FUNCTION 1` builds a one-element argument list and calls `py_len`
  directly with the caller's context: no `ProtoContext` is created for it
  (section 5).

### Example 12: higher-order functions and `import`

<!-- vm-example: 12-higher-order -->
```python
import functools


def total(xs):
    return functools.reduce(lambda a, b: a + b, xs, 0)


def sum_squares(xs):
    return sum(map(lambda x: x * x, xs))
```

```text
code 0: <module> (12-higher-order.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 34: operand stack from 0
  constants:
    #0   str     'functools'
    #1   code    <code total, line 5>  -> code 0.0
    #2   code    <code sum_squares, line 9>  -> code 0.1
    #3   NoneType None
  names:
    #0   __import__
    #1   functools
    #2   total
    #3   sum_squares
  code:
     1     0  LOAD_GLOBAL                   1  ; __import__ (+ NULL)
           2  LOAD_CONST                    0  ; 'functools'
           4  CALL_FUNCTION                 1  ; 1 positional
           6  STORE_NAME                    2  ; functools
     4     8  LOAD_CONST                    1  ; <code total, line 5>
          10  BUILD_FUNCTION                0
          12  STORE_NAME                    4  ; total
     8    14  LOAD_CONST                    2  ; <code sum_squares, line 9>
          16  BUILD_FUNCTION                0
          18  STORE_NAME                    6  ; sum_squares
          20  LOAD_CONST                    3  ; None
          22  RETURN_VALUE                  0

code 0.0: total (12-higher-order.py, line 5)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 134: variables 0..0, operand stack from 1
  constants:
    #0   code    <code <lambda>, line 5>  -> code 0.0.0
    #1   int     0
    #2   none    None
  names:
    #0   functools
    #1   reduce
  variables (slot):
    #0   xs
  code:
     5     0  LOAD_GLOBAL                   0  ; functools
           2  LOAD_ATTR                     3  ; reduce (method)
           4  LOAD_CONST                    0  ; <code <lambda>, line 5>
           6  BUILD_FUNCTION                0
           8  LOAD_FAST                     0  ; xs
          10  LOAD_CONST                    1  ; 0
          12  CALL_FUNCTION                 3  ; 3 positional
          14  RETURN_VALUE                  0
          16  LOAD_CONST                    2  ; None
          18  RETURN_VALUE                  0

code 0.0.0: <lambda> (12-higher-order.py, line 5)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 2 (0 positional-only, 0 keyword-only)
  slots 132: variables 0..1, operand stack from 2
  variables (slot):
    #0   a
    #1   b
  code:
     5     0  LOAD_FAST                     0  ; a
           2  LOAD_FAST                     1  ; b
           4  BINARY_ADD                    0
           6  RETURN_VALUE                  0

code 0.1: sum_squares (12-higher-order.py, line 9)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 133: variables 0..0, operand stack from 1
  constants:
    #0   code    <code <lambda>, line 9>  -> code 0.1.0
    #1   none    None
  names:
    #0   sum
    #1   map
  variables (slot):
    #0   xs
  code:
     9     0  LOAD_GLOBAL                   1  ; sum (+ NULL)
           2  LOAD_GLOBAL                   3  ; map (+ NULL)
           4  LOAD_CONST                    0  ; <code <lambda>, line 9>
           6  BUILD_FUNCTION                0
           8  LOAD_FAST                     0  ; xs
          10  CALL_FUNCTION                 2  ; 2 positional
          12  CALL_FUNCTION                 1  ; 1 positional
          14  RETURN_VALUE                  0
          16  LOAD_CONST                    1  ; None
          18  RETURN_VALUE                  0

code 0.1.0: <lambda> (12-higher-order.py, line 9)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 131: variables 0..0, operand stack from 1
  variables (slot):
    #0   x
  code:
     9     0  LOAD_FAST                     0  ; x
           2  LOAD_FAST                     0  ; x
           4  BINARY_MULTIPLY               0
           6  RETURN_VALUE                  0
```

- `import functools` is a call: `LOAD_GLOBAL __import__ (+ NULL)`,
  `CALL_FUNCTION 1`, `STORE_NAME functools`.
- `functools.reduce(...)`: `LOAD_ATTR 3` loads `reduce` in method form;
  `reduce` is found on the module object, not on a class, so the VM pushes
  `NULL, reduce` and the call passes no receiver.
- Each `lambda` is a nested code object built by `BUILD_FUNCTION` on every
  call of the enclosing function.
- `map` is the built-in `map` type: `CALL_FUNCTION 2` instantiates it, and
  `sum` (native) drives it. Native code calls the lambda back through
  `invokePythonCallable`; the lambda's activation creates its own context,
  the native callers do not.

### Example 13: `try` / `except` / `finally` and `raise`

<!-- vm-example: 13-exceptions -->
```python
def safe_div(a, b):
    try:
        return a / b
    except ZeroDivisionError as e:
        print("error:", e)
        return None
    finally:
        print("done")


def fail():
    raise ValueError("bad")
```

```text
code 0: <module> (13-exceptions.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code safe_div, line 2>  -> code 0.0
    #1   code    <code fail, line 12>  -> code 0.1
    #2   NoneType None
  names:
    #0   safe_div
    #1   fail
  code:
     1     0  LOAD_CONST                    0  ; <code safe_div, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; safe_div
    11     6  LOAD_CONST                    1  ; <code fail, line 12>
           8  BUILD_FUNCTION                0
          10  STORE_NAME                    2  ; fail
          12  LOAD_CONST                    2  ; None
          14  RETURN_VALUE                  0

code 0.0: safe_div (13-exceptions.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 2 (0 positional-only, 0 keyword-only)
  slots 135: variables 0..2, operand stack from 3
  constants:
    #0   str     'done'
    #1   str     'error:'
    #2   none    None
  names:
    #0   print
    #1   ZeroDivisionError
  variables (slot):
    #0   a
    #1   b
    #2   e
  code:
     3     0  SETUP_FINALLY                98  ; to 98
           2  SETUP_FINALLY                28  ; to 28
           4  LOAD_FAST                     0  ; a
           6  LOAD_FAST                     1  ; b
           8  BINARY_TRUE_DIVIDE            0
          10  POP_BLOCK                     0
          12  POP_BLOCK                     0
     8    14  LOAD_GLOBAL                   1  ; print (+ NULL)
          16  LOAD_CONST                    0  ; 'done'
          18  CALL_FUNCTION                 1  ; 1 positional
          20  POP_TOP                       0
          22  RETURN_VALUE                  0
          24  POP_BLOCK                     0
          26  JUMP_ABSOLUTE                86  ; to 86
     4    28  LOAD_GLOBAL                   2  ; ZeroDivisionError
          30  EXCEPTION_MATCH               0
          32  POP_JUMP_IF_FALSE            84  ; to 84
          34  DUP_TOP                       0
          36  STORE_FAST                    2  ; e
          38  POP_TOP                       0
          40  POP_TOP                       0
          42  POP_TOP                       0
          44  SETUP_FINALLY                80  ; to 80
     5    46  LOAD_GLOBAL                   1  ; print (+ NULL)
          48  LOAD_CONST                    1  ; 'error:'
          50  LOAD_FAST                     2  ; e
          52  CALL_FUNCTION                 2  ; 2 positional
          54  POP_TOP                       0
     6    56  LOAD_CONST                    2  ; None
          58  POP_BLOCK                     0
          60  POP_EXCEPT                    0
          62  POP_BLOCK                     0
     8    64  LOAD_GLOBAL                   1  ; print (+ NULL)
          66  LOAD_CONST                    0  ; 'done'
          68  CALL_FUNCTION                 1  ; 1 positional
          70  POP_TOP                       0
          72  RETURN_VALUE                  0
          74  POP_BLOCK                     0
          76  POP_EXCEPT                    0
          78  JUMP_ABSOLUTE                82  ; to 82
          80  RERAISE                       2
          82  JUMP_ABSOLUTE                86  ; to 86
          84  RERAISE                       1
          86  POP_BLOCK                     0
          88  LOAD_GLOBAL                   1  ; print (+ NULL)
          90  LOAD_CONST                    0  ; 'done'
          92  CALL_FUNCTION                 1  ; 1 positional
          94  POP_TOP                       0
          96  JUMP_ABSOLUTE               120  ; to 120
          98  POP_TOP                       0
         100  POP_TOP                       0
         102  POP_TOP                       0
         104  SETUP_FINALLY               118  ; to 118
         106  LOAD_GLOBAL                   1  ; print (+ NULL)
         108  LOAD_CONST                    0  ; 'done'
         110  CALL_FUNCTION                 1  ; 1 positional
         112  POP_TOP                       0
         114  POP_BLOCK                     0
         116  RERAISE                       1
         118  RERAISE                       2
         120  LOAD_CONST                    2  ; None
         122  RETURN_VALUE                  0

code 0.1: fail (13-exceptions.py, line 12)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 0 (0 positional-only, 0 keyword-only)
  slots 130: operand stack from 0
  constants:
    #0   str     'bad'
    #1   none    None
  names:
    #0   ValueError
  code:
    12     0  LOAD_GLOBAL                   1  ; ValueError (+ NULL)
           2  LOAD_CONST                    0  ; 'bad'
           4  CALL_FUNCTION                 1  ; 1 positional
           6  RAISE_VARARGS                 1  ; raise TOS
           8  POP_TOP                       0
          10  LOAD_CONST                    1  ; None
          12  RETURN_VALUE                  0
```

- `0 SETUP_FINALLY 98` (the `finally` handler) and `2 SETUP_FINALLY 28` (the
  `except` clauses) open two protected ranges, each a nested dispatch.
- `return a / b` inside the `try`: `POP_BLOCK` twice, the `finally` body
  inlined (`14`-`20`), then `RETURN_VALUE` with the quotient still on the
  stack. `24`-`26` are unreachable.
- `28`: a `ZeroDivisionError` arrives with `None, exc, exc` on the stack.
  `EXCEPTION_MATCH` keeps `exc` and pushes the test; on a match `DUP_TOP`,
  `STORE_FAST e` and three `POP_TOP`s clear the three values; the clause body
  runs under `SETUP_FINALLY 80` (a clean-up range), and its `return None`
  unwinds through `POP_BLOCK`, `POP_EXCEPT`, `POP_BLOCK` and an inlined
  `finally`.
- `84 RERAISE 1`: no clause matched; the exception leaves the inner range
  and reaches the `finally` handler at `98`, which runs the `finally` body
  and re-raises.
- `fail`: `raise ValueError("bad")` is a call and `RAISE_VARARGS 1`; the
  `POP_TOP` after it is unreachable (6).

### Example 14: decorators and comprehensions

<!-- vm-example: 14-decorators-comprehensions -->
```python
def trace(f):
    return f


@trace
def double(x):
    return 2 * x


evens = [x for x in range(10) if x % 2 == 0]
squares = {x: x * x for x in range(3)}
```

```text
code 0: <module> (14-decorators-comprehensions.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 36: operand stack from 0
  constants:
    #0   code    <code trace, line 2>  -> code 0.0
    #1   code    <code double, line 7>  -> code 0.1
    #2   code    <code <listcomp>, line 10>  -> code 0.2
    #3   int     10
    #4   code    <code <dictcomp>, line 11>  -> code 0.3
    #5   int     3
    #6   NoneType None
  names:
    #0   trace
    #1   double
    #2   range
    #3   evens
    #4   squares
  code:
     1     0  LOAD_CONST                    0  ; <code trace, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; trace
     6     6  LOAD_CONST                    1  ; <code double, line 7>
           8  BUILD_FUNCTION                0
          10  PUSH_NULL                     0
          12  ROT_TWO                       0
     5    14  LOAD_NAME                     0  ; trace
          16  ROT_TWO                       0
          18  CALL_FUNCTION                 1  ; 1 positional
          20  STORE_NAME                    2  ; double
    10    22  PUSH_NULL                     0
          24  LOAD_CONST                    2  ; <code <listcomp>, line 10>
          26  BUILD_FUNCTION                0
          28  LOAD_NAME                     5  ; range (+ NULL)
          30  LOAD_CONST                    3  ; 10
          32  CALL_FUNCTION                 1  ; 1 positional
          34  GET_ITER                      0
          36  CALL_FUNCTION                 1  ; 1 positional
          38  STORE_NAME                    6  ; evens
    11    40  PUSH_NULL                     0
          42  LOAD_CONST                    4  ; <code <dictcomp>, line 11>
          44  BUILD_FUNCTION                0
          46  LOAD_NAME                     5  ; range (+ NULL)
          48  LOAD_CONST                    5  ; 3
          50  CALL_FUNCTION                 1  ; 1 positional
          52  GET_ITER                      0
          54  CALL_FUNCTION                 1  ; 1 positional
          56  STORE_NAME                    8  ; squares
          58  LOAD_CONST                    6  ; None
          60  RETURN_VALUE                  0

code 0.0: trace (14-decorators-comprehensions.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 130: variables 0..0, operand stack from 1
  constants:
    #0   none    None
  variables (slot):
    #0   f
  code:
     2     0  LOAD_FAST                     0  ; f
           2  RETURN_VALUE                  0
           4  LOAD_CONST                    0  ; None
           6  RETURN_VALUE                  0

code 0.1: double (14-decorators-comprehensions.py, line 7)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 131: variables 0..0, operand stack from 1
  constants:
    #0   int     2
    #1   none    None
  variables (slot):
    #0   x
  code:
     7     0  LOAD_CONST                    0  ; 2
           2  LOAD_FAST                     0  ; x
           4  BINARY_MULTIPLY               0
           6  RETURN_VALUE                  0
           8  LOAD_CONST                    1  ; None
          10  RETURN_VALUE                  0

code 0.2: <listcomp> (14-decorators-comprehensions.py, line 10)
  flags 0x3 (OPTIMIZED | NEWLOCALS), parameters 1 (0 positional-only, 0 keyword-only)
  slots 134: variables 0..1, operand stack from 2
  constants:
    #0   int     2
    #1   int     0
  variables (slot):
    #0   .0
    #1   x
  code:
    10     0  BUILD_RAW_LIST                0
           2  LOAD_FAST                     0  ; .0
           4  GET_ITER                      0
           6  FOR_ITER                     28  ; to 28
           8  STORE_FAST                    1  ; x
          10  LOAD_FAST                     1  ; x
          12  LOAD_CONST                    0  ; 2
          14  BINARY_MODULO                 0
          16  LOAD_CONST                    1  ; 0
          18  COMPARE_OP                    0  ; ==
          20  POP_JUMP_IF_FALSE             6  ; to 6
          22  LOAD_FAST                     1  ; x
          24  LIST_APPEND                   2
          26  JUMP_ABSOLUTE                 6  ; to 6
          28  WRAP_RAW_LIST                 0
          30  RETURN_VALUE                  0

code 0.3: <dictcomp> (14-decorators-comprehensions.py, line 11)
  flags 0x3 (OPTIMIZED | NEWLOCALS), parameters 1 (0 positional-only, 0 keyword-only)
  slots 134: variables 0..1, operand stack from 2
  variables (slot):
    #0   .0
    #1   x
  code:
    11     0  BUILD_MAP                     0
           2  LOAD_FAST                     0  ; .0
           4  GET_ITER                      0
           6  FOR_ITER                     22  ; to 22
           8  STORE_FAST                    1  ; x
          10  LOAD_FAST                     1  ; x
          12  LOAD_FAST                     1  ; x
          14  BINARY_MULTIPLY               0
          16  LOAD_FAST                     1  ; x
          18  MAP_ADD                       3
          20  JUMP_ABSOLUTE                 6  ; to 6
          22  RETURN_VALUE                  0
```

- `@trace`: the function is built first, then `PUSH_NULL`, `ROT_TWO`,
  `LOAD_NAME trace`, `ROT_TWO` turn `fn` into `NULL trace fn`, and
  `CALL_FUNCTION 1` applies the decorator before `STORE_NAME double`.
- A comprehension is a nested function called at once with the iterator of
  its outermost iterable (`GET_ITER`, `CALL_FUNCTION 1`); inside it, the
  iterator is the parameter `.0`.
- The list comprehension accumulates in a bare `ProtoList`
  (`BUILD_RAW_LIST`), appends with `LIST_APPEND 2` (the list is two slots
  below the value: the iterator lies between them) and wraps the result once
  (`WRAP_RAW_LIST`).
- The dict comprehension pushes the value, then the key, and `MAP_ADD 3`.

### Example 15: generators

<!-- vm-example: 15-generators -->
```python
def countdown(n):
    while n > 0:
        yield n
        n -= 1


def chain(a, b):
    yield from a
    yield from b


doubled = (x * 2 for x in range(3))
```

```text
code 0: <module> (15-generators.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 36: operand stack from 0
  constants:
    #0   code    <code countdown, line 2>  -> code 0.0
    #1   code    <code chain, line 8>  -> code 0.1
    #2   code    <code <genexpr>, line 12>  -> code 0.2
    #3   int     3
    #4   NoneType None
  names:
    #0   countdown
    #1   chain
    #2   range
    #3   doubled
  code:
     1     0  LOAD_CONST                    0  ; <code countdown, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; countdown
     7     6  LOAD_CONST                    1  ; <code chain, line 8>
           8  BUILD_FUNCTION                0
          10  STORE_NAME                    2  ; chain
    12    12  PUSH_NULL                     0
          14  LOAD_CONST                    2  ; <code <genexpr>, line 12>
          16  BUILD_FUNCTION                0
          18  LOAD_NAME                     5  ; range (+ NULL)
          20  LOAD_CONST                    3  ; 3
          22  CALL_FUNCTION                 1  ; 1 positional
          24  GET_ITER                      0
          26  CALL_FUNCTION                 1  ; 1 positional
          28  STORE_NAME                    6  ; doubled
          30  LOAD_CONST                    4  ; None
          32  RETURN_VALUE                  0

code 0.0: countdown (15-generators.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only), generator
  slots 131: variables 0..0, operand stack from 1
  constants:
    #0   int     0
    #1   int     1
    #2   none    None
  variables (slot):
    #0   n
  code:
     2     0  LOAD_FAST                     0  ; n
           2  LOAD_CONST                    0  ; 0
           4  COMPARE_OP                    4  ; >
           6  POP_JUMP_IF_FALSE            24  ; to 24
     3     8  LOAD_FAST                     0  ; n
          10  YIELD_VALUE                   0
          12  POP_TOP                       0
     4    14  LOAD_FAST                     0  ; n
          16  LOAD_CONST                    1  ; 1
          18  INPLACE_SUBTRACT              0
          20  STORE_FAST                    0  ; n
          22  JUMP_ABSOLUTE                 0  ; to 0
          24  LOAD_CONST                    2  ; None
          26  RETURN_VALUE                  0

code 0.1: chain (15-generators.py, line 8)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 2 (0 positional-only, 0 keyword-only), generator
  slots 133: variables 0..1, operand stack from 2
  constants:
    #0   none    None
  variables (slot):
    #0   a
    #1   b
  code:
     8     0  LOAD_FAST                     0  ; a
           2  GET_YIELD_FROM_ITER           0
           4  LOAD_CONST                    0  ; None
           6  YIELD_FROM                    0
           8  POP_TOP                       0
     9    10  LOAD_FAST                     1  ; b
          12  GET_YIELD_FROM_ITER           0
          14  LOAD_CONST                    0  ; None
          16  YIELD_FROM                    0
          18  POP_TOP                       0
          20  LOAD_CONST                    0  ; None
          22  RETURN_VALUE                  0

code 0.2: <genexpr> (15-generators.py, line 12)
  flags 0x3 (OPTIMIZED | NEWLOCALS), parameters 1 (0 positional-only, 0 keyword-only), generator
  slots 133: variables 0..1, operand stack from 2
  constants:
    #0   int     2
    #1   none    None
  variables (slot):
    #0   .0
    #1   x
  code:
    12     0  LOAD_FAST                     0  ; .0
           2  GET_ITER                      0
           4  FOR_ITER                     20  ; to 20
           6  STORE_FAST                    1  ; x
           8  LOAD_FAST                     1  ; x
          10  LOAD_CONST                    0  ; 2
          12  BINARY_MULTIPLY               0
          14  YIELD_VALUE                   0
          16  POP_TOP                       0
          18  JUMP_ABSOLUTE                 4  ; to 4
          20  LOAD_CONST                    1  ; None
          22  RETURN_VALUE                  0
```

- `countdown` is flagged `generator`: calling it returns a generator object
  without running the body. `YIELD_VALUE` suspends it; `POP_TOP` drops the
  value `send` delivered on resumption. Each resumption runs in a new
  `ProtoContext` (1.3).
- `chain`: `yield from a` is `GET_YIELD_FROM_ITER`, `LOAD_CONST None` (the
  first value sent) and `YIELD_FROM`, which stays at its own offset while the
  sub-iterator yields.
- The generator expression is a nested generator function called with the
  iterator of `range(3)`.

### Example 16: shared state: globals, a class and attribute write groups

<!-- vm-example: 16-shared-state -->
```python
counter = 0


def bump():
    global counter
    counter += 1


class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
```

```text
code 0: <module> (16-shared-state.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 36: operand stack from 0
  constants:
    #0   int     0
    #1   code    <code bump, line 5>  -> code 0.0
    #2   str     'Point'
    #3   none    None
    #4   code    <code Point, line 10>  -> code 0.1
    #5   NoneType None
  names:
    #0   counter
    #1   bump
    #2   Point
  code:
     1     0  LOAD_CONST                    0  ; 0
           2  STORE_NAME                    0  ; counter
     4     4  LOAD_CONST                    1  ; <code bump, line 5>
           6  BUILD_FUNCTION                0
           8  STORE_NAME                    2  ; bump
     9    10  LOAD_CONST                    2  ; 'Point'
          12  BUILD_TUPLE                   0
          14  LOAD_CONST                    3  ; None
          16  LOAD_CONST                    4  ; <code Point, line 10>
          18  BUILD_FUNCTION                0
          20  BUILD_CLASS                   0
          22  STORE_NAME                    4  ; Point
          24  LOAD_CONST                    5  ; None
          26  RETURN_VALUE                  0

code 0.0: bump (16-shared-state.py, line 5)
  flags 0x3 (OPTIMIZED | NEWLOCALS), parameters 0 (0 positional-only, 0 keyword-only)
  slots 130: operand stack from 0
  constants:
    #0   int     1
    #1   none    None
  names:
    #0   counter
  code:
     6     0  LOAD_GLOBAL                   0  ; counter
           2  LOAD_CONST                    0  ; 1
           4  INPLACE_ADD                   0
           6  STORE_GLOBAL                  0  ; counter
           8  LOAD_CONST                    1  ; None
          10  RETURN_VALUE                  0

code 0.1: Point (16-shared-state.py, line 10)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 130: variables 0..0, operand stack from 1
  constants:
    #0   str     'Point'
    #1   code    <code __init__, line 11>  -> code 0.1.0
  names:
    #0   __qualname__
    #1   __init__
  code:
    10     0  LOAD_CONST                    0  ; 'Point'
           2  STORE_NAME                    0  ; __qualname__
           4  LOAD_CONST                    1  ; <code __init__, line 11>
           6  BUILD_FUNCTION                0
           8  STORE_NAME                    2  ; __init__
          10  RETURN_VALUE                  0

code 0.1.0: __init__ (16-shared-state.py, line 11)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 3 (0 positional-only, 0 keyword-only)
  slots 134: variables 0..2, operand stack from 3
  constants:
    #0   tuple   (2, 0, 1, 2)
    #1   none    None
  names:
    #0   x
    #1   y
  variables (slot):
    #0   self
    #1   x
    #2   y
  code:
    11     0  LOAD_FAST                     1  ; x
           2  LOAD_FAST                     0  ; self
           4  STORE_ATTR_GROUP              0  ; x (write 0, descriptor #0)
    12     6  LOAD_FAST                     2  ; y
           8  LOAD_FAST                     0  ; self
          10  STORE_ATTR_GROUP_END          1  ; y (write 1, descriptor #0)
          12  LOAD_CONST                    1  ; None
          14  RETURN_VALUE                  0
```

- `bump`: `global counter` makes every use a `LOAD_GLOBAL`/`STORE_GLOBAL`;
  the augmented assignment is not atomic (load, add, store).
- `class Point`: the module pushes the name, the bases tuple, `None` (no
  keywords) and the class body function, and `BUILD_CLASS` runs the body
  (code `0.1`) on the class namespace. The body stores `__qualname__` and
  `__init__` with `STORE_NAME` and ends with `RETURN_VALUE` on an empty
  stack.
- `__init__`: the two writes on `self` form an attribute write group: the
  descriptor constant `(2, 0, 1, 2)` says two writes, of names 0 (`x`) and 1
  (`y`), and that the run reads parameter slot 2 (`y`) after its first write.
  When the run applies, the object gets both attributes in one new version.

### Example 17: `with`, `*args` and f-strings

<!-- vm-example: 17-misc -->
```python
def read(path):
    with open(path) as f:
        return f.read()


def largest(*args):
    return max(*args)


def label(name, n):
    return f"{name}: {n!r}"
```

```text
code 0: <module> (17-misc.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code read, line 2>  -> code 0.0
    #1   code    <code largest, line 7>  -> code 0.1
    #2   code    <code label, line 11>  -> code 0.2
    #3   NoneType None
  names:
    #0   read
    #1   largest
    #2   label
  code:
     1     0  LOAD_CONST                    0  ; <code read, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; read
     6     6  LOAD_CONST                    1  ; <code largest, line 7>
           8  BUILD_FUNCTION                0
          10  STORE_NAME                    2  ; largest
    10    12  LOAD_CONST                    2  ; <code label, line 11>
          14  BUILD_FUNCTION                0
          16  STORE_NAME                    4  ; label
          18  LOAD_CONST                    3  ; None
          20  RETURN_VALUE                  0

code 0.0: read (17-misc.py, line 2)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 1 (0 positional-only, 0 keyword-only)
  slots 132: variables 0..1, operand stack from 2
  constants:
    #0   none    None
  names:
    #0   open
    #1   read
  variables (slot):
    #0   path
    #1   f
  code:
     2     0  LOAD_GLOBAL                   1  ; open (+ NULL)
           2  LOAD_FAST                     0  ; path
           4  CALL_FUNCTION                 1  ; 1 positional
           6  SETUP_WITH                   38  ; to 38
           8  STORE_FAST                    1  ; f
     3    10  LOAD_FAST                     1  ; f
          12  LOAD_ATTR                     3  ; read (method)
          14  CALL_FUNCTION                 0  ; 0 positional
          16  ROT_TWO                       0
          18  POP_BLOCK                     0
          20  LOAD_CONST                    0  ; None
          22  WITH_CLEANUP                  0
          24  POP_TOP                       0
          26  RETURN_VALUE                  0
          28  POP_BLOCK                     0
          30  LOAD_CONST                    0  ; None
          32  WITH_CLEANUP                  0
          34  POP_TOP                       0
          36  JUMP_ABSOLUTE                52  ; to 52
          38  SETUP_FINALLY                48  ; to 48
          40  WITH_CLEANUP                  0
          42  POP_BLOCK                     0
          44  POP_JUMP_IF_TRUE             50  ; to 50
          46  RERAISE                       1
          48  RERAISE                       2
          50  POP_EXCEPT                    0
          52  LOAD_CONST                    0  ; None
          54  RETURN_VALUE                  0

code 0.1: largest (17-misc.py, line 7)
  flags 0x17 (OPTIMIZED | NEWLOCALS | VARARGS | NESTED), parameters 0 (0 positional-only, 0 keyword-only)
  slots 132: variables 0..0, operand stack from 1
  constants:
    #0   none    None
  names:
    #0   max
  variables (slot):
    #0   args
  code:
     7     0  LOAD_GLOBAL                   1  ; max (+ NULL)
           2  BUILD_LIST                    0
           4  LOAD_FAST                     0  ; args
           6  LIST_EXTEND                   1
           8  LIST_TO_TUPLE                 0
          10  CALL_FUNCTION_EX              0  ; positional sequence
          12  RETURN_VALUE                  0
          14  LOAD_CONST                    0  ; None
          16  RETURN_VALUE                  0

code 0.2: label (17-misc.py, line 11)
  flags 0x13 (OPTIMIZED | NEWLOCALS | NESTED), parameters 2 (0 positional-only, 0 keyword-only)
  slots 134: variables 0..1, operand stack from 2
  constants:
    #0   str     ': '
    #1   none    None
  names:
    #0   repr
  variables (slot):
    #0   name
    #1   n
  code:
     1     0  LOAD_FAST                     0  ; name
    12     2  LOAD_CONST                    0  ; ': '
           4  LOAD_GLOBAL                   1  ; repr (+ NULL)
     1     6  LOAD_FAST                     1  ; n
           8  CALL_FUNCTION                 1  ; 1 positional
          10  BUILD_STRING                  3
          12  RETURN_VALUE                  0
          14  LOAD_CONST                    1  ; None
          16  RETURN_VALUE                  0
```

- `with open(path) as f`: `SETUP_WITH 38` pushes `__exit__`, calls
  `__enter__` and runs the body as a protected range. The `return` inside
  computes the value, `ROT_TWO`s it below `__exit__`, ends the range and
  calls `__exit__(None, None, None)` (`LOAD_CONST None`, `WITH_CLEANUP`).
  The handler at 38 calls `__exit__` with the exception and re-raises unless
  it returned a true value.
- `max(*args)` is `CALL_FUNCTION_EX 0`.
- `f"{name}: {n!r}"` is `BUILD_STRING 3`; `!r` is a call of the global
  `repr`. The f-string's parts carry line 1: lines inside an f-string are
  counted from the f-string (6).

### Example 18: `match`

<!-- vm-example: 18-match -->
```python
def kind(value):
    match value:
        case 0:
            return "zero"
        case [x, y]:
            return "pair"
        case _:
            return "other"
```

```text
code 0: <module> (18-match.py, line 1)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code kind, line 2>  -> code 0.0
    #1   NoneType None
  names:
    #0   kind
  code:
     1     0  LOAD_CONST                    0  ; <code kind, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; kind
           6  LOAD_CONST                    1  ; None
           8  RETURN_VALUE                  0

code 0.0: kind (18-match.py, line 2)
  flags 0x3 (OPTIMIZED | NEWLOCALS), parameters 1 (0 positional-only, 0 keyword-only)
  slots 134: variables 0..0, operand stack from 1
  constants:
    #0   int     0
    #1   str     'zero'
    #2   int     2
    #3   int     1
    #4   str     'pair'
    #5   str     'other'
    #6   none    None
  names:
    #0   __match_subj
    #1   __match_tmp_0
    #2   isinstance
    #3   str
    #4   bytes
    #5   bytearray
    #6   list
    #7   tuple
    #8   len
    #9   x
    #10  y
  variables (slot):
    #0   value
  code:
     2     0  LOAD_FAST                     0  ; value
           2  STORE_GLOBAL                  0  ; __match_subj
           4  LOAD_GLOBAL                   0  ; __match_subj
           6  JUMP_ABSOLUTE                10  ; to 10
           8  JUMP_ABSOLUTE                22  ; to 22
     3    10  LOAD_CONST                    0  ; 0
          12  COMPARE_OP                    0  ; ==
          14  POP_JUMP_IF_FALSE             8  ; to 8
     4    16  LOAD_CONST                    1  ; 'zero'
          18  RETURN_VALUE                  0
          20  JUMP_ABSOLUTE               108  ; to 108
          22  LOAD_GLOBAL                   0  ; __match_subj
          24  JUMP_ABSOLUTE                28  ; to 28
          26  JUMP_ABSOLUTE                94  ; to 94
          28  STORE_GLOBAL                  2  ; __match_tmp_0
          30  LOAD_GLOBAL                   4  ; isinstance
          32  LOAD_GLOBAL                   2  ; __match_tmp_0
          34  LOAD_GLOBAL                   6  ; str
          36  LOAD_GLOBAL                   8  ; bytes
          38  LOAD_GLOBAL                  10  ; bytearray
          40  BUILD_TUPLE                   3
          42  CALL_FUNCTION                 2  ; 2 positional
          44  POP_JUMP_IF_TRUE             26  ; to 26
          46  LOAD_GLOBAL                   4  ; isinstance
          48  LOAD_GLOBAL                   2  ; __match_tmp_0
          50  LOAD_GLOBAL                  12  ; list
          52  LOAD_GLOBAL                  14  ; tuple
          54  BUILD_TUPLE                   2
          56  CALL_FUNCTION                 2  ; 2 positional
          58  POP_JUMP_IF_FALSE            26  ; to 26
          60  LOAD_GLOBAL                  16  ; len
          62  LOAD_GLOBAL                   2  ; __match_tmp_0
          64  CALL_FUNCTION                 1  ; 1 positional
          66  LOAD_CONST                    2  ; 2
          68  COMPARE_OP                    0  ; ==
          70  POP_JUMP_IF_FALSE            26  ; to 26
          72  LOAD_GLOBAL                   2  ; __match_tmp_0
          74  LOAD_CONST                    0  ; 0
          76  BINARY_SUBSCR                 0
          78  STORE_GLOBAL                 18  ; x
          80  LOAD_GLOBAL                   2  ; __match_tmp_0
          82  LOAD_CONST                    3  ; 1
          84  BINARY_SUBSCR                 0
          86  STORE_GLOBAL                 20  ; y
     6    88  LOAD_CONST                    4  ; 'pair'
          90  RETURN_VALUE                  0
          92  JUMP_ABSOLUTE               108  ; to 108
          94  LOAD_GLOBAL                   0  ; __match_subj
          96  JUMP_ABSOLUTE               100  ; to 100
          98  JUMP_ABSOLUTE               108  ; to 108
         100  POP_TOP                       0
     8   102  LOAD_CONST                    5  ; 'other'
         104  RETURN_VALUE                  0
         106  JUMP_ABSOLUTE               108  ; to 108
         108  POP_TOP                       0
         110  LOAD_CONST                    6  ; None
         112  RETURN_VALUE                  0
```

- There are no pattern opcodes: `match` compiles to comparisons and calls
  (`Compiler::compileMatch`, `compilePattern`). The subject and each
  pattern's candidate are stored in temporaries (`__match_subj`,
  `__match_tmp_0`), which inside a function are **globals**
  (`STORE_GLOBAL`), as are the capture names `x` and `y` (6, issue 3).
- The sequence pattern calls `isinstance` and `len` with the legacy call
  layout (no NULL marker: `LOAD_GLOBAL 4` has bit 0 clear), which
  `CALL_FUNCTION` misreads when anything lies below on the stack (6,
  issue 3).
- The `JUMP_ABSOLUTE` pairs at `6`/`8`, `24`/`26`, `96`/`98` are the
  trampolines `compilePattern` uses as failure targets.

### Example 19: coroutines, `async with`, `async for`

<!-- vm-example: 19-async -->
```python
async def fetch(source):
    async with source as s:
        return await s.read()


async def collect(stream):
    out = []
    async for item in stream:
        out.append(item)
    return out
```

```text
code 0: <module> (19-async.py, line 6)
  flags 0x0, parameters 0 (0 positional-only, 0 keyword-only)
  slots 33: operand stack from 0
  constants:
    #0   code    <code fetch, line 2>  -> code 0.0
    #1   code    <code collect, line 7>  -> code 0.1
    #2   NoneType None
  names:
    #0   fetch
    #1   collect
  code:
     6     0  LOAD_CONST                    0  ; <code fetch, line 2>
           2  BUILD_FUNCTION                0
           4  STORE_NAME                    0  ; fetch
           6  POP_TOP                       0
    11     8  LOAD_CONST                    1  ; <code collect, line 7>
          10  BUILD_FUNCTION                0
          12  STORE_NAME                    2  ; collect
          14  POP_TOP                       0
          16  LOAD_CONST                    2  ; None
          18  RETURN_VALUE                  0

code 0.0: fetch (19-async.py, line 2)
  flags 0x113 (OPTIMIZED | NEWLOCALS | NESTED | COROUTINE), parameters 1 (0 positional-only, 0 keyword-only), generator
  slots 138: variables 0..1, operand stack from 2
  constants:
    #0   none    None
    #1   NoneType None
  names:
    #0   __aenter__
    #1   __aexit__
    #2   __class__
    #3   read
  variables (slot):
    #0   source
    #1   s
  code:
     2     0  LOAD_FAST                     0  ; source
           2  DUP_TOP                       0
           4  LOAD_ATTR                     1  ; __aenter__ (method)
           6  CALL_FUNCTION                 0  ; 0 positional
           8  GET_AWAITABLE                 0
          10  LOAD_CONST                    0  ; None
          12  YIELD_FROM                    0
          14  STORE_FAST                    1  ; s
          16  SETUP_FINALLY                58  ; to 58
     3    18  LOAD_FAST                     1  ; s
          20  LOAD_ATTR                     7  ; read (method)
          22  CALL_FUNCTION                 0  ; 0 positional
          24  GET_AWAITABLE                 0
          26  LOAD_CONST                    0  ; None
          28  YIELD_FROM                    0
          30  RETURN_VALUE                  0
          32  POP_BLOCK                     0
          34  DUP_TOP                       0
          36  LOAD_ATTR                     3  ; __aexit__ (method)
          38  LOAD_CONST                    0  ; None
          40  LOAD_CONST                    0  ; None
          42  LOAD_CONST                    0  ; None
          44  CALL_FUNCTION                 3  ; 3 positional
          46  GET_AWAITABLE                 0
          48  LOAD_CONST                    0  ; None
          50  YIELD_FROM                    0
          52  POP_TOP                       0
          54  POP_TOP                       0
          56  JUMP_ABSOLUTE                98  ; to 98
          58  POP_TOP                       0
          60  ROT_TWO                       0
          62  POP_TOP                       0
          64  DUP_TOP                       0
          66  LOAD_ATTR                     4  ; __class__
          68  ROT_TWO                       0
          70  LOAD_CONST                    0  ; None
          72  BUILD_TUPLE                   3
          74  ROT_TWO                       0
          76  LOAD_ATTR                     2  ; __aexit__
          78  ROT_TWO                       0
          80  PUSH_NULL                     0
          82  ROT_THREE                     0
          84  CALL_FUNCTION_EX              0  ; positional sequence
          86  GET_AWAITABLE                 0
          88  LOAD_CONST                    0  ; None
          90  YIELD_FROM                    0
          92  POP_JUMP_IF_TRUE             96  ; to 96
          94  RERAISE                       1
          96  POP_EXCEPT                    0
          98  POP_TOP                       0
         100  LOAD_CONST                    1  ; None
         102  RETURN_VALUE                  0

code 0.1: collect (19-async.py, line 7)
  flags 0x113 (OPTIMIZED | NEWLOCALS | NESTED | COROUTINE), parameters 1 (0 positional-only, 0 keyword-only), generator
  slots 135: variables 0..2, operand stack from 3
  constants:
    #0   NoneType None
  names:
    #0   append
  variables (slot):
    #0   stream
    #1   item
    #2   out
  code:
     8     0  BUILD_LIST                    0
           2  STORE_FAST                    2  ; out
           4  LOAD_FAST                     0  ; stream
           6  GET_AITER                     0
           8  FOR_ITER                     24  ; to 24
          10  STORE_FAST                    1  ; item
     9    12  LOAD_FAST                     2  ; out
          14  LOAD_ATTR                     1  ; append (method)
          16  LOAD_FAST                     1  ; item
          18  CALL_FUNCTION                 1  ; 1 positional
          20  POP_TOP                       0
          22  JUMP_ABSOLUTE                 8  ; to 8
    10    24  LOAD_FAST                     2  ; out
          26  RETURN_VALUE                  0
          28  LOAD_CONST                    0  ; None
          30  RETURN_VALUE                  0
```

- `fetch` and `collect` are coroutines (`COROUTINE` flag, `generator`).
  `await e` is `GET_AWAITABLE`, `LOAD_CONST None`, `YIELD_FROM`.
- `async with`: `__aenter__` is called and awaited, the body runs under
  `SETUP_FINALLY 58`; the normal exit and the handler fetch `__aexit__`
  afresh and await it (the handler calls it through `CALL_FUNCTION_EX`
  with `(type(exc), exc, None)`). `SETUP_ASYNC_WITH` is not used.
- `async for` is `GET_AITER` and an ordinary `FOR_ITER` loop: `GET_AITER`
  gives a class-defined async iterator a `__next__` that drives `__anext__`.
- The module listing shows two compiler defects around `async def` at module
  level: `POP_TOP` after each `STORE_NAME` (6, issue 2) and line numbers that
  do not match the source (6).

## 4. How to add an opcode

Model the work on a real, simple opcode: `DUP_TOP_TWO` (stack manipulation)
or `POP_JUMP_IF_TRUE` (a jump). Every place to touch, in order:

1. **The enum**, `include/protoPython/ExecutionEngine.h`: a new
   `constexpr int OP_NAME = 220;` with a comment giving the operand, the
   stack effect and the semantics. Do not reuse 167 or a reserved value
   without checking `_opcode` users.
2. **The name table**, `opcodeName` in `src/library/Disassembler.cpp`, and
   its operand comment in `Lister::annotate`; also `opmap` and `py_has_arg`
   in `src/library/OpcodeModule.cpp` (the `_opcode` module) and
   `opcodeHasArg` in `ExecutionEngine.cpp` (diagnostic tracing).
3. **The stack effect**, `stackEffect` in `src/library/Compiler.cpp`. A
   missing entry counts 0, which under-sizes the frame for an opcode that
   pushes.
4. **The emission site** in the compiler (`Compiler::emit(OP_NAME, arg)`).
   A forward jump is emitted with argument 0 and patched with
   `addPatch(bytecodeOffset() - 1, target)` (the index of the instruction,
   not of its argument word); absolute targets are offsets (`2 *` the
   instruction index). Never shift instructions after `applyPatches`.
5. **The VM case** in `Activation::dispatch` (`ExecutionEngine.cpp`):
   - read operands with `stack.back()` / `stack[stack.top - k]` and keep them
     in their slots until the result replaces them; push a partial result as
     an extra slot while you allocate more (`BUILD_TUPLE` does);
   - raise with `env->raiseTypeError(...)` and friends (they throw); never
     return a null result to signal an error;
   - `break` to continue at the next instruction, or set `i` and `continue`
     to jump; a `continue` without changing `i` runs the same instruction
     again;
   - an opcode that opens a protected range must follow `SETUP_FINALLY`
     (`runRange`) and the `levelExit` protocol.
6. **The peephole specialiser** (`specialiseBytecode`) only if the new
   opcode should take part in a fused window, and the debugger's handler
   analysis (`Activation::debugRangeHandles`) if it can appear in an
   `except` clause's type expression.
7. **Tests:** a unit test in `test/library/TestExecutionEngine.cpp` (it
   builds code objects by hand), a regression script in `test/regression/`
   for the Python construct (CONTRIBUTING.md, "Adding a regression test"),
   and a `docs/vm-examples/NN-name.py` whose listing shows the opcode.
8. **This document:** the opcode reference (section 2, and the counts at its
   top), then `python3 docs/vm-examples/disasm_examples.py <protopy> --update`
   and a review of the regenerated listings.

`protopyc` compiles from the AST and needs no change for a new opcode.

## 5. How to add a primitive

A primitive is a native method: a C++ function with protoCore's method
signature, stored as an attribute of a module or a type prototype.

```cpp
static const proto::ProtoObject* py_len(
    proto::ProtoContext* context,               // the CALLER's context
    const proto::ProtoObject* self,             // the object the method was made for
    const proto::ParentLink* parentLink,
    const proto::ProtoArguments* positionalParameters,
    const proto::ProtoKeywordArguments* keywordParameters);
```

- **Registration.** `ctx->fromMethod(owner, fn)` makes the method value; the
  built-ins are set on the `builtins` module in `BuiltinsModule.cpp`
  (`builtins->setAttribute(ctx, PythonEnvironment::getInternedString(ctx,
  "len"), ctx->fromMethod(builtins, py_len))`), native modules in their
  `initialize` (`OpcodeModule.cpp` is a short one), type methods on the
  prototypes in `PythonEnvironment::initializeRootObjects`. Use interned
  names (`getInternedString`).
- **Arguments.** `positionalParameters->getSize(ctx)` and `getAt(ctx, i)`;
  keyword arguments by the interned name's hash through
  `keywordParameters`. Check the count yourself and raise CPython's
  `TypeError`: nothing checks it for you (6, issue 8 shows what happens
  otherwise).
- **Errors.** Raise with `env->raiseTypeError(ctx, "...")`,
  `raiseValueError`, ... (`PythonEnvironment::fromContext(ctx)` gives the
  environment). They throw a C++ exception; there is no pending flag and no
  null-return protocol, except that a native `__next__` signals exhaustion by
  returning `nullptr` without raising.
- **Calling back into Python.** `invokePythonCallable(ctx, callable, args,
  kwargs)` or `env->callObject`.
- **Rooting.** protoCore EMBEDDER-CONFORMANCE rule 3: a `ProtoObject*` held
  only in a C++ local across an allocation, a call into Python or a
  safepoint can be reclaimed. The argument list is pinned by the caller for
  the duration of the call (`invokeCallable`'s `TransientPin`); a value you
  create and keep while you allocate more must be pinned
  (`PythonEnvironment::TransientPin pin(env, obj)`) or stored in an object
  that is itself rooted. Read docs/GC_BRIDGING.md first.
- **The context rule.** A `ProtoContext` is created only when a method,
  interpreted code with its own locals, is activated; a primitive runs in
  the caller's context. protoPython's `main` follows it: `invokeCallable`
  (`ExecutionEngine.cpp`) calls a native method with the context it was
  given, which is the calling activation's; the only places that create a
  context are the activations of 1.3 (`runUserFunctionCall`,
  `runUserFunctionCallRaw`, `py_generator_resume`, `runCodeObject`) and
  thread start-up. What a primitive allocates is therefore garbage of the
  caller's context, released when that activation ends or at its next
  safepoint; a primitive that loops a long time without calling back into
  Python should call `ctx->safepoint()` only when every live value is
  rooted.
- **Test.** A regression script in `test/regression/` (CONTRIBUTING.md) that
  checks the result and the errors with `assert`, registered in the
  top-level `CMakeLists.txt`.

## 6. Known oddities and issues

### Verified bugs

Each was reproduced with `protopy` built from `main` at `d453de62` and
compared with CPython 3.14. None is fixed by the change that added this
document.

1. **The peephole specialiser fuses across a jump target.** A jump into the
   middle of a fused window lands on a `NOP`, skipping the fused work.
   `while (x if c else y) < b:` compiles to `... JUMP_ABSOLUTE 10` (the
   `x` branch) and `8 LT_FAST_FAST_JF` (`y < b`) with offset 10 a `NOP`: on
   the `x` branch the comparison never runs and the loop body is entered.
   `def f(c, x, y, b): while (x if c else y) < b: return "lt"` then `return
   "ge"`: `f(True, 5, 0, 3)` returns `"lt"` (CPython: `"ge"`). Cause:
   `Compiler::specialiseBytecode` assumes jumps into a window are harmless.
2. **A stray `POP_TOP` after some statements pops the loop iterator.**
   `Compiler::statementLeavesValue` lists the statements that leave nothing
   on the stack; `from m import x`, an annotated assignment (`x: int = 1`),
   `type T = ...`, `match`, `nonlocal`, `async def` and `raise` are missing,
   so each is followed by a `POP_TOP`. Outside a loop it pops an empty stack
   (a no-op); inside a `for` loop it pops the iterator and the program
   **hangs**: `for i in range(3):` followed by `x: int = 1` never ends, at
   module level and in functions alike (examples 07 and 19 show the
   `POP_TOP`).
3. **`match` uses globals and a legacy call layout.** Inside a function the
   subject, the temporaries and the capture names are stored with
   `STORE_GLOBAL` (example 18): after `kind([1, 2])`, `x` and `y` are
   module globals. A sequence pattern calls `isinstance` without a NULL
   marker; inside a `for` loop `CALL_FUNCTION` takes the iterator below for
   the callable: `TypeError: '<list_iterator object at ...>' object is not
   callable`.
4. **`<<=` and `>>=` truncate integers.** `INPLACE_LSHIFT` and
   `INPLACE_RSHIFT` compute on 64-bit `long long`: `x = 1; x <<= 70` gives
   0 (CPython: 1180591620717411303424) and `y = -8; y >>= 70` gives 0
   (CPython: -1); a bignum left operand would overflow `asLong`. The binary
   opcodes are correct.
5. **A starred assignment target in a function is a global.**
   `first, *others = rest` stores `others` with `STORE_GLOBAL`
   (example 09): the value leaks into the module's globals.
6. **Asynchronous comprehensions never finish.** In
   `Compiler::compileComprehension`'s async branch, three `addPatch(slot +
   1, ...)` calls patch the argument of the instruction after the intended
   one (`SETUP_FINALLY 0`, `GET_ANEXT 22`, `POP_TOP 38` in the listing), and
   the `JUMP_FORWARD` is given an absolute target although the VM treats it
   as relative. `[x async for x in agen()]` yields `None` forever
   (`compileAsyncWith` has the off-by-one fixed, per its comment).
7. **A module, class body or `exec` that raises does not release its
   context.** `runCodeObject` deletes its heap-allocated `ProtoContext` and
   restores the current context only on a normal return. Measured symptom:
   `exec("raise ValueError")` caught in a loop takes 0.65 s for 5,000
   iterations, 1.49 s for 10,000 and 24.6 s (295 MB) for 20,000, while
   `exec("x = 1")` takes 1.2 s (141 MB) for 20,000.
8. **`len()` with no argument returns `None`** instead of raising
   `TypeError`: `py_len` returns `PROTO_NONE` when the count is short.

### Oddities

- **`CALL_FUNCTION` guesses the call layout** from the stack depth (1.5).
  It is right for every call the compiler emits with a NULL marker; issue 3
  is the case where it is not.
- **Underflow guards that re-dispatch.** A few handlers (`BUILD_LIST`,
  `BUILD_SET`, `BUILD_MAP`, `BUILD_STRING`, `BUILD_SLICE`, `FORMAT_VALUE`,
  `LOAD_GLOBAL` without an environment) `continue` without advancing the
  instruction pointer on an impossible stack shape, which would run the same
  instruction forever. Compiler output never reaches them.
- **Two `None` constants.** Function bodies use protoCore's `PROTO_NONE`
  (kind `none`); the module's final `return None` and an async function's use
  the environment's `None` object (kind `NoneType`, `compileModule`,
  `compileAsyncFunctionDef`). Both behave as `None`.
- **`CO_NESTED` (0x10) is not CPython's flag.** It is set when the capture
  analysis of `compileFunctionDef` reports any candidate free name, which
  happens for functions that read no free variable (`parity` in example 03
  has it, `sign` does not; the cause was not established). The VM never
  reads it.
- **Line numbers.** `co_firstlineno` is the line of the first statement of
  the body, not of the `def`; a lambda's is one more than its line; the
  parts of an f-string carry line 1; the module listing of example 19 is
  off around `async def`.
- **Dead code after `return`/`raise`.** The compiler does not drop
  unreachable instructions (`JUMP_ABSOLUTE` after a `return` in an `if`
  branch, the implicit `return None` after an explicit `return`, the
  `POP_TOP` after `raise`).
- **Stale comments.** `executeBytecodeRange` says the safepoint is polled
  every 256 opcodes; the loop polls every 64. `docs/EXECUTION_ENGINE_OPCODES.md`
  (now superseded) counted 116 opcodes up to 216; 217-219 were added since.
- **`RERAISE`'s argument** (1 or 2) is never read.
- **The in-place division opcodes** do not treat a native `__itruediv__` /
  `__ifloordiv__` / `__imod__` returning `NotImplemented` as absent, unlike
  the other in-place opcodes (code reading; not reproduced).
- **`_opcode`** lacks 8 opcodes and its `stack_effect` returns 0 for every
  opcode (`OpcodeModule.cpp`, a stub).
