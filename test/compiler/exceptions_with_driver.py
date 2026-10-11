# Drives exceptions_with_probe (test/compiler/protopyc_exceptions_with.sh): the
# output of the compiled module must equal the interpreter's on the source.
import exceptions_with_probe as m


def show(label, fn, *args):
    try:
        print(label, fn(*args))
    except Exception as e:
        print(label, "raised", type(e).__name__, str(e))


def drain(label):
    print(label, m.LOG)
    del m.LOG[:]


print("module", m.MOD)
drain("module log")

show("caller_sees", m.caller_sees)
show("builtin_raises", m.builtin_raises)
show("argument_raises", m.argument_raises)
show("raise_class", m.raise_class)
show("unmatched", m.unmatched)
show("calls_unmatched", m.calls_unmatched)
show("reraise", m.reraise)
drain("reraise log")
show("handler_raises", m.handler_raises)
show("raising_tests", m.raising_tests)

show("in_loop", m.in_loop, 6)
show("loop_stops", m.loop_stops, 10)
drain("loop_stops log")
show("bad_iter", m.bad_iter)
drain("bad_iter log")

show("fin clean", m.call_fin, False)
show("fin raising", m.call_fin, True)
drain("fin log")
show("return_in_finally", m.return_in_finally)
show("return_through_finally", m.return_through_finally)
drain("return_through_finally log")
show("finally_overrides_return", m.finally_overrides_return)
show("break_continue_finally", m.break_continue_finally)
for k in range(3):
    show("nested " + str(k), m.nested, k)
show("deep_return", m.deep_return, 5)
drain("deep_return log")

acct = m.Account(10)
show("withdraw", acct.withdraw, 4)
show("withdraw too much", acct.withdraw, 40)
show("try_withdraw", acct.try_withdraw, 40)
show("balance", lambda: acct.balance)

show("with_basic", m.with_basic)
drain("with_basic log")
show("with_raise", m.with_raise)
drain("with_raise log")
show("with_suppress", m.with_suppress)
drain("with_suppress log")
show("with_nested", m.with_nested)
drain("with_nested log")
show("with_multi", m.with_multi)
drain("with_multi log")
show("with_return", m.with_return)
drain("with_return log")
show("with_loop", m.with_loop)
drain("with_loop log")
show("with_truthy", m.with_truthy)

G = m.make_greeter("hi ")
g = G()
print("greet", g.greet("a"), g.greet("b"), g.count())
bound = g.greet
print("bound", bound("c"))
print("through class", G.greet(g, "d"))
print("compiled caller", m.use_greeter())
H = m.make_greeter("hey ")
print("two classes", H().greet("e"), g.greet("f"))

Loud = m.make_loud("yo ")
print("subclass", Loud().greet("g"), Loud().twice("h"), Loud().count())

# Exceptions and with statements in a loop while collections run.
total = 0
for i in range(3000):
    total += len(m.in_loop(4)) + len(m.nested(i % 3)) + len(m.break_continue_finally())
    if m.with_suppress() != "after suppress":
        print("with_suppress failed")
    del m.LOG[:]
    total += len(G().greet(str(i)))
print("loop total", total)
print("END")
