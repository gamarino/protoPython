# Execution Engine Opcode Coverage (Phase 3)

This document is the Phase 3 execution engine audit: for each opcode, whether it is implemented in the engine and which CTest (if any) covers it.

## Opcode coverage matrix

| Opcode | Code | Engine | Test |
|--------|------|--------|------|
| LOAD_CONST | 100 | Y | LoadConstReturnValue, LoadConstString |
| RETURN_VALUE | 101 | Y | (all tests) |
| LOAD_NAME | 102 | Y | LoadNameStoreName |
| STORE_NAME | 103 | Y | LoadNameStoreName |
| BINARY_ADD | 104 | Y | BinaryAdd |
| BINARY_SUBTRACT | 105 | Y | BinarySubtract |
| CALL_FUNCTION | 106 | Y | CallFunction |
| BINARY_MULTIPLY | 107 | Y | BinaryMultiply |
| BINARY_TRUE_DIVIDE | 108 | Y | BinaryTrueDivide |
| COMPARE_OP | 109 | Y | CompareOp |
| POP_JUMP_IF_FALSE | 110 | Y | none |
| JUMP_ABSOLUTE | 111 | Y | none |
| LOAD_ATTR | 112 | Y | LoadAttr |
| STORE_ATTR | 113 | Y | StoreAttr |
| BUILD_LIST | 114 | Y | BuildList |
| BINARY_SUBSCR | 115 | Y | BinarySubscr |
| BUILD_MAP | 116 | Y | BuildMap |
| STORE_SUBSCR | 117 | Y | none |
| BUILD_TUPLE | 118 | Y | BuildTuple |
| GET_ITER | 119 | Y | GetIterNoCrash |
| FOR_ITER | 120 | Y | none (GetIterNoCrash does not exercise loop) |
| UNPACK_SEQUENCE | 121 | Y | UnpackSequence |
| LOAD_GLOBAL | 122 | Y | LoadGlobalStoreGlobal |
| STORE_GLOBAL | 123 | Y | LoadGlobalStoreGlobal |
| BUILD_SLICE | 124 | Y | BuildSlice |
| ROT_TWO | 125 | Y | RotTwo |
| DUP_TOP | 126 | Y | DupTop |
| BINARY_MODULO | 127 | Y | none |
| BINARY_POWER | 128 | Y | BinaryPower |
| BINARY_FLOOR_DIVIDE | 129 | Y | BinaryFloorDivide |
| UNARY_NEGATIVE | 130 | Y | none |
| UNARY_NOT | 131 | Y | none |
| UNARY_INVERT | 132 | Y | UnaryInvert |
| POP_TOP | 133 | Y | PopTop |
| UNARY_POSITIVE | 134 | Y | none |
| NOP | 135 | Y | Nop |
| INPLACE_ADD | 136 | Y | InplaceAdd |
| BINARY_LSHIFT | 137 | Y | BinaryLshift |
| BINARY_RSHIFT | 138 | Y | none |
| INPLACE_SUBTRACT | 139 | Y | InplaceSubtract |
| BINARY_AND | 140 | Y | BinaryAnd |
| BINARY_OR | 141 | Y | none |
| BINARY_XOR | 142 | Y | none |
| INPLACE_MULTIPLY | 143 | Y | InplaceMultiply |
| INPLACE_TRUE_DIVIDE | 144 | Y | InplaceTrueDivide |
| INPLACE_FLOOR_DIVIDE | 145 | Y | none |
| INPLACE_MODULO | 146 | Y | InplaceModulo |
| INPLACE_POWER | 147 | Y | none |
| INPLACE_LSHIFT | 148 | Y | InplaceLshift |
| INPLACE_RSHIFT | 149 | Y | none |
| INPLACE_AND | 150 | Y | InplaceAnd |
| INPLACE_OR | 151 | Y | none |
| INPLACE_XOR | 152 | Y | none |
| ROT_THREE | 153 | Y | RotThree |
| ROT_FOUR | 154 | Y | RotFour |
| DUP_TOP_TWO | 155 | Y | DupTopTwo |

## Gaps (before Step 2)

Opcodes with no dedicated test: STORE_SUBSCR, FOR_ITER (real loop), BINARY_MODULO, UNARY_NEGATIVE, UNARY_NOT, UNARY_POSITIVE, BINARY_RSHIFT, BINARY_OR, BINARY_XOR, INPLACE_FLOOR_DIVIDE, INPLACE_POWER, INPLACE_RSHIFT, INPLACE_OR, INPLACE_XOR, POP_JUMP_IF_FALSE, JUMP_ABSOLUTE.

All opcodes 100–155 are implemented in [ExecutionEngine.cpp](../src/library/ExecutionEngine.cpp). Tests are in [TestExecutionEngine.cpp](../test/library/TestExecutionEngine.cpp). Run with: `ctest -R test_execution_engine` or `./build/test/library/test_execution_engine`.
