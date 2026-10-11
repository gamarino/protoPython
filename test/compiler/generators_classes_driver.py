# Drives generators_classes_probe (test/compiler/protopyc_generators_classes.sh):
# the output of the compiled module must equal the interpreter's on the source.
import generators_classes_probe as m

print("count_up", list(m.count_up(3, 7)), list(m.count_up(0, 10, 3)))

g = m.running_totals(5)
out = []
while True:
    try:
        out.append(next(g))
    except StopIteration as e:
        print("running_totals", out, "return", e.value)
        break

e = m.echo(5)
print("echo", next(e), e.send(6), e.send(7))
try:
    e.send(None)
except StopIteration as stop:
    print("echo stop", stop.value)

c = m.catcher()
print("catcher", next(c), c.throw(ValueError("boom")), next(c))

gd = m.guarded("x")
print("guarded", next(gd))
gd.close()
print("guarded closed", m.LOG)
print("guarded full", list(m.guarded("y")), m.LOG)

print("outer", list(m.outer(4)))
print("branches", list(m.branches(7)))
print("closure gen", list(m.make_counter(2)(3)))
print("nones", list(m.nones(3)))

total = 0
for _ in range(3):
    for r in m.rows(20000):
        total += r[0] + len(r[1]) + r[2]["k"]
print("rows", total)

a = m.count_up(0, 30000)
b = m.rows(30000)
mixed = 0
for _ in range(30000):
    mixed += next(a) + next(b)[2]["k"]
print("interleaved", mixed)

done = m.count_up(0, 1)
print("exhausted", list(done), list(done))

p = m.make_point().moved(10)
print("point", p.x, p.y, p.scaled().x, p.describe(), m.Point.origin_name)
print("later", m.later_caller())
print("nested", m.nested_def(5))
print("lambda", m.lambda_global()(7), m.lambda_closure(4)(5))
bag = m.Bag([1, 2, 3, 4, 6])
print("bag", list(bag), [b.items for b in bag.evens()], bag.total(), m.sum_bag(m.Bag([5, 5])))
print("END")
