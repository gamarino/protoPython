# Regression test: the peephole specialiser must not fuse a window that a jump
# enters part-way.
#
# Compiler::specialiseBytecode replaces `LOAD_FAST a; LOAD_FAST b; COMPARE_OP <;
# POP_JUMP_IF_FALSE t` (and two INPLACE_ADD windows) by one fused instruction
# followed by NOPs.  `while (x if c else y) < b` compiles the `x` branch to a
# jump straight to `LOAD_FAST b`, the second instruction of such a window: the
# jump landed on a NOP, the comparison never ran and the loop body was entered.
# `f(True, 5, 0, 3)` returned "lt" (CPython: "ge").


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


def while_cond(c, x, y, b):
    while (x if c else y) < b:
        return "lt"
    return "ge"


check("while, x branch, not less", while_cond(True, 5, 0, 3), "ge")
check("while, y branch, less", while_cond(False, 5, 0, 3), "lt")
check("while, x branch, less", while_cond(True, 1, 9, 3), "lt")
check("while, y branch, not less", while_cond(False, 1, 9, 3), "ge")


def if_cond(c, x, y, b):
    if (x if c else y) < b:
        return "lt"
    return "ge"


check("if, x branch", if_cond(True, 5, 0, 3), "ge")
check("if, y branch", if_cond(False, 5, 0, 3), "lt")


def or_operand(p, a, b):
    # `p or a` leaves either value, then `< b`: the `p` path jumps to the
    # comparison's second load.
    if (p or a) < b:
        return "lt"
    return "ge"


check("or, first operand", or_operand(7, 0, 3), "ge")
check("or, second operand", or_operand(0, 1, 3), "lt")


def acc(c, total, x, y):
    # A conditional expression as the right operand of `+=`: the window
    # LOAD_FAST total; LOAD_FAST x; INPLACE_ADD; STORE_FAST total is entered at
    # INPLACE_ADD from the `y` branch.
    total += (x if c else y)
    return total


check("+=, x branch", acc(True, 10, 1, 100), 11)
check("+=, y branch", acc(False, 10, 1, 100), 110)


def inc_loop(n):
    # The fused forms still apply where no jump enters the window.
    i = 0
    s = 0
    while i < n:
        s += i
        i += 1
    return s


check("fused loop", inc_loop(10), 45)
print("peephole_jump_targets: ok")
