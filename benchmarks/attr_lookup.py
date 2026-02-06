class FastObject:
    def __init__(self, a, b, c):
        self.a = a
        self.b = b
        self.c = c

def run_bench(n=100000):
    obj = FastObject(1, 2, 3)
    total = 0
    for _ in range(n):
        total += obj.a
        total += obj.b
        total += obj.c
    return total

if __name__ == "__main__":
    import sys
    n = 100000
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
    run_bench(n)
