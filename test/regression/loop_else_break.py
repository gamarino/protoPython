"""break / continue inside a loop's `else:` clause act on the ENCLOSING loop.

The else clause runs after the loop has finished, so it is outside that loop:
`break` there leaves the enclosing loop (functools._c3_merge relies on it).
"""
import asyncio


def for_else_break():
    n = 0
    for a in [1, 2, 3]:
        n += 1
        assert n <= 3, "runaway outer loop"
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
        seen.append(a)                     # never reached
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


def inner_break_skips_else():
    out = []
    for a in [1, 2]:
        for b in [10, 20]:
            if b == 20:
                break
        else:
            out.append('else')
        out.append(a)
    return out


def c3_like(sequences):
    """The shape of functools._c3_merge's candidate search."""
    result = []
    while True:
        sequences = [s for s in sequences if s]
        if not sequences:
            return result
        for s1 in sequences:
            candidate = s1[0]
            for s2 in sequences:
                if candidate in s2[1:]:
                    candidate = None
                    break
            else:
                break
        if candidate is None:
            raise RuntimeError("Inconsistent hierarchy")
        result.append(candidate)
        assert len(result) < 10, "runaway merge"
        for seq in sequences:
            if seq[0] == candidate:
                del seq[0]


async def async_for_else_break():
    async def agen():
        yield 1

    n = 0
    for a in [1, 2, 3]:
        n += 1
        async for _ in agen():
            pass
        else:
            break
    return n


assert for_else_break() == 1
assert for_else_continue() == []
assert while_else_break() == 1
assert inner_break_skips_else() == [1, 2]
assert c3_like([['C', 'B', 'object'], ['B', 'object'], ['B']]) == ['C', 'B', 'object']
assert asyncio.run(async_for_else_break()) == 1

# Module level as well as function level.
n = 0
for a in [1, 2, 3]:
    n += 1
    assert n <= 3, "runaway outer loop"
    for b in [10]:
        pass
    else:
        break
assert n == 1

import functools
assert functools._c3_mro(object) == [object]
assert functools._c3_mro(bool) == [bool, int, object]
print("loop else break OK")
