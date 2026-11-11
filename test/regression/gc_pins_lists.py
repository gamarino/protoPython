# Native list, sort, min/max and dict-equality code holds a container's
# storage (or values gathered from an iterator) in C++ locals while Python
# code runs: an __eq__ or a key function that replaces the storage and
# allocates must not let the collector free what the walk still reads. Run
# with PROTOCORE_GC_CONTEXT_THRESHOLD=100 so a collection runs inside the
# Python code. A freed and recycled cell shows up as an object without `t`
# (AttributeError) or as a crash.
import sys

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 else 40


class K:
    def __init__(self, t):
        self.t = t

    def __eq__(self, other):
        return isinstance(other, K) and other.t == self.t

    def __lt__(self, other):
        return self.t < other.t

    __hash__ = None


class Z:
    def __init__(self, u):
        self.u = u


def _alloc(i):
    return [Z(i), (i, i), object()]


def churn():
    # Many calls (each a context, so several collections at the threshold)
    # that allocate objects of the freed ones' shapes.
    keep = None
    for i in range(1500):
        keep = _alloc(i)
    return keep


class State:
    budget = 0
    target = None


def mutate():
    if State.budget > 0:
        State.budget -= 1
        c = State.target
        if isinstance(c, list):
            c.clear()
            c.extend(K(i) for i in range(40))
        elif isinstance(c, dict):
            c.clear()
            for i in range(40):
                c[i] = K(i)
        churn()


class Mutator(K):
    def __eq__(self, other):
        mutate()
        # A recycled cell has no `t`: that is the use-after-free.
        return isinstance(other, K) and other.t == self.t


def arm(container, n=3):
    State.target = container
    State.budget = n


def key(x):
    mutate()
    return x.t


def items():
    return [K(i) for i in range(30)]


def case_list_in():
    lst = items()
    arm(lst)
    assert Mutator(5) in lst
    lst = items()
    arm(lst)
    assert lst.__contains__(Mutator(5))
    lst = items()
    arm(lst)
    assert Mutator(29) in lst


def case_list_count():
    lst = items()
    arm(lst)
    assert lst.count(Mutator(5)) >= 1


def case_list_index():
    lst = items()
    arm(lst)
    assert lst.index(Mutator(5)) == 5


def case_list_remove():
    lst = items()
    arm(lst)
    lst.remove(Mutator(5))


def case_list_compare():
    lst = items()
    other = [Mutator(i) for i in range(30)]
    arm(lst)
    other == lst
    arm(lst)
    other < lst


def case_list_sort():
    lst = items()
    arm(lst)
    try:
        lst.sort(key=key)
    except ValueError:
        pass  # list modified during sort, as in CPython


def case_sorted_key():
    lst = items()
    arm(lst)
    out = sorted((K(i) for i in range(30)), key=key)
    assert [x.t for x in out] == list(range(30))


def case_min_max_key():
    lst = items()
    arm(lst)
    assert max((K(i) for i in range(30)), key=key).t == 29
    arm(lst)
    assert min((K(i) for i in range(30)), key=key).t == 0


def case_dict_eq():
    d1 = {i: Mutator(i) for i in range(30)}
    d2 = {i: K(i) for i in range(30)}
    arm(d2)
    d1 == d2
    arm(d1)
    d2 == d1


def case_dict_views():
    d = {i: K(i) for i in range(30)}
    arm(d)
    (3, Mutator(3)) in d.items()
    arm(d)
    Mutator(3) in d.values()


CASES = [case_list_in, case_list_count, case_list_index, case_list_remove, case_list_compare, case_list_sort, case_sorted_key, case_min_max_key, case_dict_eq, case_dict_views]
selected = [c for c in CASES if len(sys.argv) < 3 or c.__name__ == "case_" + sys.argv[2]]
for r in range(ROUNDS):
    for case in selected:
        case()

print("gc_pins_lists: ok")
