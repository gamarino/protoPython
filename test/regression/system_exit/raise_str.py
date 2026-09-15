# A non-integer SystemExit code is printed to stderr and exits with 1
# (expected: 1).
raise SystemExit("bye")
