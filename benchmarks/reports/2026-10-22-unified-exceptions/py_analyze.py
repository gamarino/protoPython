import re, os, statistics as st, math, collections
M = os.path.dirname(os.path.abspath(__file__)); O = os.path.join(M, "py_out")
OLD = "/home/gamarino/Documentos/proyectos/protoPython/benchmarks/reports/2026-10-21-cpp-exceptions-final"
def parse(f):
    d = {}
    for l in open(f):
        c = [x.strip() for x in l.strip().strip("|").split("|")]
        if len(c) == 8 and not c[0].startswith(("Benchmark", "---")):
            name = c[0].replace(" [INFO]", "")
            num = lambda s: float(s) if re.match(r"^[0-9.]+$", s) else None
            d[name] = {"cpt": num(c[1]), "cp": num(c[2]), "py": num(c[4]), "pyc": num(c[6])}
    return d
R = {v: [parse(os.path.join(O, "py_%s_r%d.md" % (v, r))) for r in (1, 2, 3)] for v in ("before", "after")}
old = [parse(os.path.join(OLD, "py_r%d.md" % r)) for r in (1, 2, 3)]
def med(runs, b, k):
    xs = [r[b][k] for r in runs if b in r and r[b][k] is not None]
    return st.median(xs) if xs else None
names = list(R["after"][0])
print("| Benchmark | CPython 3.14t | protopy BEFORE | protopy AFTER | AFTER/BEFORE | protopyc BEFORE | protopyc AFTER | AFTER/BEFORE | old protopy / protopyc (10-21, 8a367f65) |")
print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
G = collections.defaultdict(list)
for n in names:
    cpt = st.median([x for v in R for x in [med(R[v], n, "cpt")] if x])
    pb, pa = med(R["before"], n, "py"), med(R["after"], n, "py")
    cb, ca = med(R["before"], n, "pyc"), med(R["after"], n, "pyc")
    f = lambda x: "%.1f" % x if x else "-"
    r = lambda a, b: "%.3f" % (a / b) if a and b else "-"
    if n != "memory_pressure":
        if pa and pb: G["py_ab"].append(pa / pb); G["py_a_cpt"].append(med(R["after"], n, "py") / med(R["after"], n, "cpt")); G["py_b_cpt"].append(pb / med(R["before"], n, "cpt"))
        if ca and cb: G["pyc_ab"].append(ca / cb); G["pyc_a_cpt"].append(ca / med(R["after"], n, "cpt")); G["pyc_b_cpt"].append(cb / med(R["before"], n, "cpt"))
    print("| `%s` | %s | %s | %s | %s | %s | %s | %s | %s / %s |" % (n, f(cpt), f(pb), f(pa), r(pa, pb), f(cb), f(ca), r(ca, cb), f(med(old, n, "py")), f(med(old, n, "pyc"))))
gm = lambda xs: math.exp(sum(map(math.log, xs)) / len(xs))
for k, v in G.items(): print("geomean %-10s n=%2d %.3f" % (k, len(v), gm(v)))
print("\nraw protopy/protopyc per round:")
for n in names: print(" ", n, {v: [(r[n]["py"], r[n]["pyc"]) for r in R[v]] for v in R})
