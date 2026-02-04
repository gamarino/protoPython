# str_concat_loop.py - Benchmark: s = ""; s = s + "x" for i in range(N)
N = 10000

def main():
    s = ""
    for i in range(N):
        s = s + "x"
    return len(s)

if __name__ == "__main__":
    main()
