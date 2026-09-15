# Regression test: SystemExit.code follows CPython.
#
# SystemExit.__init__ never set `code`, so the top-level handler found no code
# and exited with status 0 for `raise SystemExit(3)` or
# `raise SystemExit(main())`; only sys.exit(int) set it by hand, and
# sys.exit(non-int) dropped its argument.
import sys


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


check("no argument", SystemExit().code, None)
check("one argument", SystemExit(3).code, 3)
check("string argument", SystemExit("bye").code, "bye")
check("several arguments", SystemExit(1, 2).code, (1, 2))

for arg in (None, 4, "msg"):
    try:
        sys.exit(arg)
    except SystemExit as exc:
        check("sys.exit(%r)" % (arg,), exc.code, arg)

try:
    sys.exit()
except SystemExit as exc:
    check("sys.exit()", exc.code, None)

print("SystemExit.code OK")
