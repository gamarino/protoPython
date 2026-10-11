# Drives gc_rooting_probe.py compiled by protopyc (protoPython issue #2): each
# workload runs three times so collections fall inside compiled calls, and the
# result is checked, so a freed cell is a wrong answer or a crash.
import gc_rooting_probe as m
N = 20000
checks = [
    ("build", lambda: m.build(N), N),
    ("for_sum", lambda: m.for_sum(N), 2 * N),
    ("closure_calls", lambda: m.closure_calls(N), N),
    ("comprehensions", lambda: m.comprehensions(2000), 2000 * (8 + 4 + 3 + 10)),
    ("raises", lambda: m.raises(N), N),
    ("lambdas", lambda: m.lambdas(N), 2 * N),
    ("strings", lambda: m.strings(N), N),
    ("module_rows", lambda: len(m.MODULE_ROWS), 3000),
]
bad = 0
for name, fn, want in checks:
    try:
        got = [fn() for _ in range(3)]
    except Exception as e:
        got = "EXC " + repr(e)
    ok = got == [want] * 3
    bad += not ok
    print(name, "OK" if ok else "BAD got=%r want=%r" % (got, want), flush=True)
print("FAILED" if bad else "ALL OK")
if bad:
    raise SystemExit(1)
