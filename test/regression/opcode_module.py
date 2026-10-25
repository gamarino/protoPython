# Regression test: protopy's `_opcode` module names every opcode and gives
# real stack effects.
#
# _opcode.opmap lacked eight opcodes (PUSH_NULL, the raw-list pair, the fused
# opcodes, the attribute write groups) and stack_effect returned 0 for every
# opcode (OpcodeModule.cpp was a stub).  The numbers are protopy's own
# (docs/VM.md, section 2), not CPython's.
import _opcode


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


op = _opcode.opmap
for name in ("PUSH_NULL", "WRAP_RAW_LIST", "BUILD_RAW_LIST", "ACC_FAST_FAST", "INC_FAST_K",
             "LT_FAST_FAST_JF", "STORE_ATTR_GROUP", "STORE_ATTR_GROUP_END", "LOAD_CLOSURE",
             "LOAD_CLASSDEREF", "DELETE_DEREF"):
    assert name in op, "opmap lacks " + name
check("opname agrees with opmap", _opcode.opname[op["PUSH_NULL"]], "PUSH_NULL")
check("LOAD_FROM_DICT_OR_DEREF alias", op["LOAD_FROM_DICT_OR_DEREF"], op["LOAD_CLASSDEREF"])

se = _opcode.stack_effect
check("POP_TOP", se(op["POP_TOP"]), -1)
check("BINARY_ADD", se(op["BINARY_ADD"]), -1)
check("DUP_TOP", se(op["DUP_TOP"]), 1)
check("LOAD_CONST", se(op["LOAD_CONST"], 0), 1)
check("BUILD_TUPLE 3", se(op["BUILD_TUPLE"], 3), -2)
check("CALL_FUNCTION 2", se(op["CALL_FUNCTION"], 2), -3)
check("STORE_SUBSCR", se(op["STORE_SUBSCR"]), -3)
check("DELETE_SUBSCR", se(op["DELETE_SUBSCR"], 0), -2)
check("FOR_ITER, no jump", se(op["FOR_ITER"], 0, jump=False), 1)
check("FOR_ITER, jump", se(op["FOR_ITER"], 0, jump=True), -1)
check("FOR_ITER, either", se(op["FOR_ITER"], 0), 1)
check("SETUP_FINALLY, handler", se(op["SETUP_FINALLY"], 0, jump=True), 3)

for bad, args in (("unknown opcode", (3,)), ("missing oparg", (op["LOAD_CONST"],)),
                  ("unexpected oparg", (op["POP_TOP"], 1))):
    try:
        se(*args)
    except ValueError:
        pass
    else:
        raise AssertionError(bad + ": no ValueError")
print("opcode_module: ok")
