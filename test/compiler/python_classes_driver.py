# Drives python_classes_probe (test/compiler/protopyc_python_classes.sh): the
# output of the compiled module must equal python_classes.expected, CPython 3's
# output on the same source (the interpreter must print it too).
import python_classes_probe as m


def show(label, fn, *args):
    # One line per result: the interpreter's repr of a list stops after 20
    # items.
    try:
        results = fn(*args)
    except Exception as e:
        print(label, "raised", type(e).__name__, str(e))
        return
    for k, r in enumerate(results):
        print(label, k, r)


show("attribute_reads", m.attribute_reads)
show("class_facts", m.class_facts)
show("closures", m.closures)
show("class_machinery", m.class_machinery)
show("chaining", m.chaining)


# Classes the interpreter defines on compiled bases.
class Child(m.Derived):
    def describe(self):
        return "Child/" + super().describe()


def interpreted_subclasses():
    out = []
    c = Child("cy")
    out.append([k.__name__ for k in Child.__mro__])
    out.append(c.greet())
    out.append(c.describe())
    out.append(repr(c))
    out.append(isinstance(c, m.Base))
    out.append(len(c))

    class MoreManaged(m.Managed):
        def __enter__(self):
            self.events.append("sub enter")
            return super().__enter__()
    with MoreManaged() as mm:
        mm.events.append("body")
    out.append(mm.events)

    class ChildError(m.ProbeError):
        pass
    try:
        raise ChildError(3)
    except m.ProbeError as e:
        out.append((type(e).__name__, e.code))

    class Late(m.Registry):
        pass
    out.append(m.SUBCLASSES[-1])
    return out


def interpreted_with():
    out = []
    with m.Managed() as cm:
        cm.events.append("body")
    out.append(cm.events)
    mgr = m.Managed()
    with mgr:
        raise KeyError("suppressed")
    out.append(mgr.events)
    try:
        with m.Managed() as cm2:
            raise ValueError("propagates")
    except ValueError:
        out.append(cm2.events)
    return out


def interpreted_access():
    out = []
    p = m.Plain(2)
    out.append(p.double)
    out.append(m.Plain.make(3).x)
    out.append(p.method())
    h = m.lambda_method_class(9)()
    out.append(h.get())
    t = m.attached_method_class("z")()
    out.append(t.outside())
    out.append(t.late())
    out.append(m.make_local_class("t")("x").describe())
    return out


show("interpreted_subclasses", interpreted_subclasses)
show("interpreted_with", interpreted_with)
show("interpreted_access", interpreted_access)
print("END")
