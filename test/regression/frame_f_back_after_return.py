# Regression test: a call's f_back is its caller's frame, also after the
# caller has called something else.
#
# runUserFunctionCall made the callee's frame the thread's current frame and
# never restored the caller's, so after `f()` returned the current frame was
# still f's: the next call from the same caller took the previous callee as
# its f_back.  Besides the wrong answer, that linked every frame-mapped call
# into one chain of mutable frames, f_back to f_back, which protoCore releases
# one level per collection cycle: a loop of 60,000 calls of
# `def scale(xs, k): return [x * k for x in xs]` grew the live set by about 75
# cells per call.  Every value below is CPython's.
import sys


def f():
    def inner():
        pass
    return sys._getframe().f_back.f_code.co_name


def leaf():
    return 1


def main():
    def keep():
        pass
    names = [f(), f(), f()]
    leaf()
    names.append(f())
    return names


assert main() == ["main", "main", "main", "main"], main()


def mapped(k):
    # A nested scope captures k, so this function's frame is mutable.
    g = lambda: k
    return g()


def caller():
    def keep():
        pass
    mapped(1)
    return f()


assert caller() == "caller", caller()
print("f_back after return OK")
