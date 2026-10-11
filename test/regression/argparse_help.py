# argparse beyond nargs: several prefix characters, help and usage output
# (through textwrap), and textwrap itself.  argparse_help.expected is
# CPython's output.
import argparse
import textwrap

p = argparse.ArgumentParser(prog="p", prefix_chars="-+", exit_on_error=False)
p.add_argument("+x", nargs="*")
p.add_argument("-y")
print(p.parse_args(["+x", "1", "2", "-y", "3"]))
try:
    p.add_argument("z_bad", "--zz")
except ValueError as e:
    print("ValueError", e)

p = argparse.ArgumentParser(prog="tool", description="Process some files.",
                            epilog="See the manual for more.")
p.add_argument("files", nargs="*", help="input files to read")
p.add_argument("--count", "-c", type=int, default=3, help="how many times to repeat each line")
p.add_argument("--mode", choices=["fast", "slow"], help="the processing mode")
p.add_argument("--flag", action="store_true", help="a boolean switch")
g = p.add_mutually_exclusive_group()
g.add_argument("--quiet", action="store_true")
g.add_argument("--verbose", action="store_true")
p.print_help()
print(p.format_usage(), end="")
print(repr(p.parse_args(["a", "-c", "5", "--mode", "fast"])))

print(textwrap.wrap("The quick brown fox jumps over the lazy dog " * 3, width=30))
print(textwrap.fill("one two three four five six", width=10))
print(textwrap.shorten("Hello  world, this is a long line", width=20))
print(repr(textwrap.dedent("    a\n      b\n    c\n")), repr(textwrap.indent("x\ny\n", "> ")))
