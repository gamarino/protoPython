# The compiler sizes each frame's operand stack from the stack effect of every
# opcode it emits (Compiler::stackEffect). An effect off by one in either
# direction drifts with each use: too high and a function that repeats a
# construct gets an ever larger frame, too low and it overflows the stack.
# The frame size of a function must not depend on how often a construct
# repeats in it (review round 6, M3).
snips = {
 'call_name': 'g(1)',
 'call_kw': 'g(1, k=2)',
 'call_ex': 'g(*a)',
 'call_exkw': 'g(*a, **kw)',
 'call_attr': 'o.m(1)',
 'call_attr_kw': 'o.m(1, k=2)',
 'call_attr_ex': 'o.m(*a)',
 'call_expr': '(g)(1)',
 'call_sub': 'L[0](1)',
 'with': 'with cm: pass',
 'with_as': 'with cm as x: pass',
 'for': 'for x in L: pass',
 'for_break': 'for x in L:\n        break',
 'try': 'try:\n        pass\n    except Exception:\n        pass',
 'try_as': 'try:\n        g(1)\n    except Exception as e:\n        pass',
 'try_finally': 'try:\n        pass\n    finally:\n        pass',
 'listcomp': '[x for x in L]',
 'dictcomp': '{x: x for x in L}',
 'genexp': 'list(x for x in L)',
 'fstring': 'f"{a}{b!r}{c:>3}"',
 'chaincmp': '1 < b < 3',
 'augsub': 'L[0] += 1',
 'augattr': 'o.v += 1',
 'unpack': 'x, y = 1, 2',
 'starunpack': 'x, *y = L',
 'importfrom': 'from os import path',
 'import': 'import os',
 'assert': 'assert b',
 'del': 'D["k"] = 1\n    del D["k"]',
 'lambda_def': 'h = lambda q=1: q',
 'ifexp': 'x = 1 if b else 2',
 'boolop': 'x = b and c or a',
 'slice': 'x = L[1:2]',
 'slice3': 'x = L[::2]',
 'dictlit': 'x = {1: 2, 3: 4}',
 'setlit': 'x = {1, 2}',
 'super_call': 'pass',
 'walrus': '(z := 3)',
 'match': 'match b:\n        case 1:\n            pass\n        case _:\n            pass',
 'nested_def': 'def inner(q=1): return q',
 'while': 'while False: pass',
 'global_load': 'x = a',
 'isinst': 'isinstance(a, int)',
 'starcall_list': 'g(*L, 1, *L)',
 'raise': 'try:\n        raise ValueError(1)\n    except ValueError:\n        pass',
}

pre = "class O:\n    v=0\n    def m(s,*a,**k): return 0\no=O()\ndef g(*a, **k): return 0\na=(1,)\nb=2\nc=3\nkw={}\nL=[1,2]\nD={}\nimport contextlib\ncm=contextlib.nullcontext()\n"

def frame_slots(body):
    ns = {}
    exec(compile(pre + body, "<drift>", "exec"), ns)
    return ns["f"].__code__.co_automatic_count

wide = "    w = [" + ",".join("1.5" for _ in range(40)) + "]\n"
drift = []
for name, s in snips.items():
    once = "def f():\n    " + s + "\n" + wide + "    return 0\n"
    many = "def f():\n" + ("    " + s + "\n") * 50 + wide + "    return 0\n"
    a, b = frame_slots(once), frame_slots(many)
    if a != b:
        drift.append((name, a, b))
assert not drift, drift
print("stack_effect_drift: ok")
