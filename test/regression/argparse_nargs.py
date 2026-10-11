# argparse nargs variants (and the actions that take several values) against
# CPython: argparse_nargs.expected is CPython's output.
import argparse


def run(spec, argv):
    p = argparse.ArgumentParser(prog="p", exit_on_error=False)
    for args, kw in spec:
        p.add_argument(*args, **kw)
    try:
        return p.parse_args(argv)
    except argparse.ArgumentError as e:
        return "ArgumentError: " + str(e)
    except SystemExit as e:
        return "SystemExit " + str(e.code)


cases = [
    ([(("files",), dict(nargs="*"))], []),
    ([(("files",), dict(nargs="*"))], ["a", "b"]),
    ([(("--opt",), dict(nargs="*"))], ["--opt"]),
    ([(("--opt",), dict(nargs="*"))], ["--opt", "x", "y"]),
    ([(("--opt",), dict(nargs="*"))], []),
    ([(("--opt",), dict(nargs="*", default=["d"]))], []),
    ([(("files",), dict(nargs="*")), (("--opt",), dict(nargs="*"))], ["a", "b", "--opt", "x", "y"]),
    ([(("files",), dict(nargs="*")), (("-v",), dict(action="store_true"))], ["a", "-v", "b"]),
    ([(("x",), dict(nargs="+"))], ["a", "b"]),
    ([(("x",), dict(nargs="?", default="D"))], []),
    ([(("x",), dict(nargs="?", default="D"))], ["q"]),
    ([(("--o",), dict(nargs="?", const="C", default="D"))], ["--o"]),
    ([(("--o",), dict(nargs="?", const="C", default="D"))], []),
    ([(("--o",), dict(nargs="?", const="C", default="D"))], ["--o", "v"]),
    ([(("x",), dict(nargs=2))], ["a", "b"]),
    ([(("x",), dict(nargs=argparse.REMAINDER))], ["a", "--b", "c"]),
    ([(("a",), {}), (("rest",), dict(nargs="*", type=int))], ["s", "1", "2"]),
    ([(("--n",), dict(nargs="*", type=int, choices=[1, 2, 3]))], ["--n", "1", "3"]),
    ([(("--n",), dict(nargs="+", action="append"))], ["--n", "1", "2", "--n", "3"]),
    ([(("--n",), dict(nargs="*", action="extend"))], ["--n", "1", "2", "--n", "3"]),
    ([(("first",), {}), (("mid",), dict(nargs="*")), (("last",), {})], ["a", "b", "c", "d"]),
    ([(("-x",), dict(nargs="*")), (("pos",), dict(nargs="?"))], ["-x", "1", "2"]),
    ([(("--pair",), dict(nargs=2, metavar=("K", "V")))], ["--pair", "k", "v"]),
]
for spec, argv in cases:
    print(argv, run(spec, argv))

# Errors for a missing required count (CPython exits with status 2 here too).
for spec, argv in [([(("x",), dict(nargs="+"))], []), ([(("x",), dict(nargs=2))], ["a"])]:
    print(argv, run(spec, argv))

p = argparse.ArgumentParser(prog="tool", description="d")
p.add_argument("files", nargs="*", help="input files")
p.add_argument("--opt", nargs="?", const=1, help="an option")
print(p.format_usage().strip())
