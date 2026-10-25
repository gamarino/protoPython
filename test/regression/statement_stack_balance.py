# Regression test: no statement may leave a stray POP_TOP behind it.
#
# Compiler::statementLeavesValue decides which statements are expression
# statements (whose value is popped).  `from m import x`, an annotated
# assignment, `type T = ...`, `match`, `nonlocal`, `async def`, `raise`,
# `async with` and `import a, b` (a suite of imports) were missing from its
# list of statements, so each was followed by a POP_TOP.  Inside a `for` loop
# that POP_TOP removed the iterator and the loop never ended.  ctest's timeout
# catches a regression.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


n = 0
for i in range(3):
    from os import sep
    n += 1
check("from-import in a module-level loop", n, 3)

n = 0
for i in range(3):
    x: int = i
    n += 1
check("annotated assignment in a module-level loop", (n, x), (3, 2))

n = 0
for i in range(3):
    import os, sys
    n += 1
check("import a, b in a module-level loop", n, 3)

n = 0
for i in range(3):
    type Alias = list[int]
    n += 1
check("type alias in a module-level loop", n, 3)

n = 0
for i in range(3):
    match i:
        case 0:
            pass
        case _:
            pass
    n += 1
check("match in a module-level loop", n, 3)

n = 0
for i in range(3):
    async def coro():
        return 1
    n += 1
check("async def in a module-level loop", n, 3)

n = 0
for i in range(3):
    try:
        raise ValueError(i)
    except ValueError:
        n += 1
check("raise in a module-level loop", n, 3)


def in_function():
    count = 0
    total = 0

    def bump():
        nonlocal count
        count += 1

    for i in range(4):
        from os import sep
        y: int = i
        type T = int
        match i:
            case 1:
                total += 10
            case _:
                total += 1

        def inner():
            nonlocal total
            total += 0
        inner()

        async def c():
            return 0
        bump()
    return count, total, y


check("statements in a function loop", in_function(), (4, 13, 3))
print("statement_stack_balance: ok")
