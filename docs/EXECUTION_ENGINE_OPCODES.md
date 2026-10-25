# Execution Engine Opcodes (superseded)

This reference is superseded by [VM.md](VM.md), which documents every opcode
of `include/protoPython/ExecutionEngine.h` (119 opcodes, codes 100 to 219,
167 unused) with its operand, stack effect, semantics and the construct that
emits it, together with the instruction encoding, the fused opcodes of the
peephole specialiser, the attribute write groups and worked examples taken
from `protopy --disasm`.

Where the topics of this page went:

| Topic | VM.md section |
|---|---|
| Instruction format | [1.2 Instruction encoding and the code object](VM.md#12-instruction-encoding-and-the-code-object) |
| Fused opcodes | [1.2](VM.md#12-instruction-encoding-and-the-code-object) and [2.11 Fused opcodes](VM.md#211-fused-opcodes-peephole-specialiser) |
| Attribute write groups | [2.6 Attributes and subscripts](VM.md#26-attributes-and-subscripts) |
| Opcode table | [2. Opcode reference](VM.md#2-opcode-reference) |

The execution engine's unit tests are in
[`test/library/TestExecutionEngine.cpp`](../test/library/TestExecutionEngine.cpp):

```bash
ctest --test-dir build_release -R test_execution_engine --output-on-failure
```
