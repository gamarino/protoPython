# itertools against CPython (itertools_functions.expected is its output):
# every function, None values, generators and Python iterators as inputs,
# laziness on infinite inputs, argument errors, the combinatorics.
import itertools as it
import operator


def t(name, f):
    try:
        r = f()
    except Exception as e:
        r = "EXC " + type(e).__name__
    print(name, r)


def gen(*xs):
    for x in xs:
        yield x


class PyIter:
    def __init__(self, n):
        self.i, self.n = 0, n
    def __iter__(self):
        return self
    def __next__(self):
        if self.i >= self.n:
            raise StopIteration
        self.i += 1
        return self.i


t("chain", lambda: list(it.chain([1, None, 2], (), "ab", [None])))
t("chain gens", lambda: list(it.chain(gen(1, None), PyIter(2), gen())))
t("chain.from_iterable", lambda: list(it.chain.from_iterable([[1, 2], [None], [], "xy"])))
t("from_iterable lazy", lambda: list(it.islice(it.chain.from_iterable(it.repeat([1, 2])), 5)))
t("from_iterable gen", lambda: list(it.chain.from_iterable([i] * i for i in range(4))))
t("chain type", lambda: (type(it.chain()) is it.chain, isinstance(it.chain.from_iterable([]), it.chain)))
t("count", lambda: list(it.islice(it.count(5, 2), 4)))
t("count float", lambda: list(it.islice(it.count(0.5, 0.25), 3)))
t("count repr", lambda: (repr(it.count(3)), repr(it.count(1, 2))))
t("cycle", lambda: list(it.islice(it.cycle("abc"), 7)))
t("cycle gen", lambda: list(it.islice(it.cycle(gen(None, 1)), 5)))
t("cycle empty", lambda: list(it.cycle([])))
t("repeat", lambda: (list(it.repeat(None, 3)), list(it.repeat("x", -1)), repr(it.repeat(1, 2))))
t("accumulate", lambda: list(it.accumulate([1, 2, 3, 4])))
t("accumulate mul", lambda: list(it.accumulate([1, 2, 3, 4], operator.mul)))
t("accumulate initial", lambda: list(it.accumulate([1, 2, 3], initial=10)))
t("accumulate empty initial", lambda: list(it.accumulate([], initial=5)))
t("batched", lambda: list(it.batched("abcdefg", 3)))
t("batched strict", lambda: list(it.batched("abcd", 3, strict=True)))
t("batched zero", lambda: list(it.batched("ab", 0)))
t("compress", lambda: list(it.compress("abcdef", [1, 0, 1, 0, 1, 1])))
t("dropwhile", lambda: list(it.dropwhile(lambda x: x < 3, [1, 2, 3, 4, 1])))
t("takewhile", lambda: list(it.takewhile(lambda x: x < 3, gen(1, 2, 3, 1))))
t("filterfalse", lambda: list(it.filterfalse(lambda x: x % 2, range(8))))
t("filterfalse None", lambda: list(it.filterfalse(None, [0, 1, None, 2, "", 3])))
t("groupby", lambda: [(k, list(g)) for k, g in it.groupby("aaabbcdd")])
t("groupby key", lambda: [(k, list(g)) for k, g in it.groupby([1, 3, 2, 4, 5], key=lambda x: x % 2)])
t("groupby skip", lambda: [k for k, g in it.groupby("aabbbc")])
t("groupby stale", lambda: (lambda gs: [list(g) for g in gs])([g for k, g in it.groupby("aab")]))
t("islice", lambda: list(it.islice(range(20), 2, 15, 3)))
t("islice None", lambda: list(it.islice([None, None, 1], 3)))
t("islice stop None", lambda: list(it.islice("abcd", 1, None)))
t("islice consume", lambda: (lambda g: (list(it.islice(g, 2)), list(g)))(iter(range(5))))
t("islice negative", lambda: list(it.islice([1], -1)))
t("islice step 0", lambda: list(it.islice([1], 0, 1, 0)))
t("pairwise", lambda: list(it.pairwise("abcd")))
t("pairwise short", lambda: list(it.pairwise("a")))
t("starmap", lambda: list(it.starmap(pow, [(2, 3), (3, 2)])))
t("tee", lambda: [list(x) for x in it.tee([1, 2, 3], 2)])
t("tee interleaved", lambda: (lambda a, b: (next(a), next(a), next(b), list(b), list(a)))(*it.tee(gen(1, 2, 3))))
t("tee zero", lambda: it.tee([1], 0))
t("zip_longest", lambda: list(it.zip_longest("ab", "xyz", fillvalue="-")))
t("zip_longest none", lambda: list(it.zip_longest()))
t("product", lambda: list(it.product("ab", [0, 1])))
t("product repeat", lambda: list(it.product([0, 1], repeat=2)))
t("product empty", lambda: (list(it.product()), list(it.product([], [1]))))
t("permutations", lambda: list(it.permutations("abc", 2)))
t("permutations full", lambda: list(it.permutations(range(3))))
t("permutations r>n", lambda: list(it.permutations("ab", 3)))
t("combinations", lambda: list(it.combinations("abcd", 2)))
t("combinations r>n", lambda: list(it.combinations("ab", 3)))
t("combinations 0", lambda: list(it.combinations("ab", 0)))
t("cwr", lambda: list(it.combinations_with_replacement("abc", 2)))
t("cwr empty", lambda: list(it.combinations_with_replacement("", 2)))
t("counts", lambda: (len(list(it.permutations(range(5)))), len(list(it.combinations(range(10), 4))),
                     sum(1 for _ in it.product(range(10), repeat=3))))
t("tuple str", lambda: (tuple("ab"), tuple("ab")[0] == "a"))
