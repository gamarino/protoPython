"""
Mandelbrot set — float arithmetic benchmark.

Computes the iteration count for each pixel of a SIZE×SIZE Mandelbrot
image.  The hot loop does complex-number arithmetic using real+imag pairs
(no Python complex type, to avoid boxing overhead and keep the comparison
focused on float arithmetic and loop control).

Stresses: BINARY_MULTIPLY, BINARY_ADD, BINARY_SUBTRACT, COMPARE_OP
(float), FOR_ITER / WHILE loop overhead.  No list mutation in the hot
path — the only list written to is the per-row accumulator, updated once
per inner loop exit.

Fair across implementations: CPython has no special float fast-path in
the bytecode dispatch (floats are heap objects in both interpreters); the
ratio reflects the actual cost of float boxing and arithmetic dispatch.

Usage:  python3 bench_mandelbrot.py        # default size=150
        python3 bench_mandelbrot.py 200    # heavier
"""
import sys
import time


def mandelbrot(size, max_iter=50):
    count = 0
    y = 0
    while y < size:
        ci = 2.0 * y / size - 1.0
        x = 0
        while x < size:
            cr = 2.0 * x / size - 1.5
            zr = 0.0
            zi = 0.0
            i = 0
            while i < max_iter:
                zr2 = zr * zr
                zi2 = zi * zi
                if zr2 + zi2 > 4.0:
                    break
                zi = 2.0 * zr * zi + ci
                zr = zr2 - zi2 + cr
                i += 1
            if i < max_iter:
                count += 1
            x += 1
        y += 1
    return count


def main():
    size = 150
    if len(sys.argv) > 1:
        size = int(sys.argv[1])

    mandelbrot(size)  # warmup
    times = []
    for _ in range(5):
        t0 = time.perf_counter()
        result = mandelbrot(size)
        times.append((time.perf_counter() - t0) * 1000)

    times.sort()
    print("mandelbrot size=%d  min=%.1fms  median=%.1fms  escaped=%d" %
          (size, times[0], times[2], result))


if __name__ == "__main__":
    main()
