# Constructs whose C++ lowering used `long` (32 bits on Windows) and GCC's
# __builtin_mul_overflow: SmallInt multiplication, `*=` on a local, a class body
# (namespace keys walked as pointers) and every function's argument count.
def mul(a, b):
    return a * b


def power_of_three(n):
    total = 1
    for i in range(n):
        total *= 3
    return total


class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y


BIG = mul(1099511627776, 1099511627776)
SMALL = mul(-7, 6)
POW = power_of_three(39)
P = Point(2, 3)
AREA = P.x * P.y
