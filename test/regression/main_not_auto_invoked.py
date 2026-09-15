# Regression test: protopy must not call a module-level main() on its own.
#
# The runtime used to invoke `main` after executing a script, a leftover from
# the early stub execution path.  A script that calls main() itself, directly
# or under `if __name__ == "__main__":`, ran it twice (every print inside it
# appeared twice), and a script that only defines main() ran it once where
# CPython runs it zero times.

calls = []


def main():
    calls.append(__name__)
    if len(calls) > 1:
        raise AssertionError("main() invoked %d times" % len(calls))


if __name__ == "__main__":
    main()

assert calls == ["__main__"], calls
print("main not auto-invoked OK")
