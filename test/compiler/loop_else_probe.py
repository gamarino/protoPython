# break / continue in a loop's `else:` clause act on the ENCLOSING loop, in a
# module protopyc compiled (the interpreter: test/regression/loop_else_break.py).
def for_else_break():
    n = 0
    for a in [1, 2, 3]:
        n += 1
        for b in [10]:
            pass
        else:
            break
    return n


def for_else_continue():
    seen = []
    for a in [1, 2, 3]:
        for b in [10]:
            pass
        else:
            continue
        seen.append(a)
    return seen


def while_else_break():
    n = 0
    while n < 5:
        n += 1
        i = 0
        while i < 2:
            i += 1
        else:
            break
    return n


def while_else_continue():
    n = 0
    seen = []
    while n < 3:
        n += 1
        i = 0
        while i < 1:
            i += 1
        else:
            continue
        seen.append(n)
    return seen


def inner_break_skips_else():
    out = []
    for a in [1, 2]:
        for b in [10, 20]:
            if b == 20:
                break
        else:
            out.append(0)
        out.append(a)
    return out


def else_runs_on_exhaustion():
    out = []
    for a in [1]:
        out.append(a)
    else:
        out.append(2)
    i = 0
    while i < 1:
        i += 1
    else:
        out.append(3)
    return out
