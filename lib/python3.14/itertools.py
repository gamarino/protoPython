"""Functional tools for creating and using iterators.

protoPython's itertools, written in Python after the pure-Python equivalents
given in CPython's documentation.  It replaces a native module that skipped
generators and other Python-level iterators, ended a chain or an islice at the
first None it produced, built chain.from_iterable eagerly (never returning for
an infinite input), and returned empty iterators for groupby, permutations,
combinations and combinations_with_replacement.

Infinite iterators:
count(start=0, step=1) --> start, start+step, start+2*step, ...
cycle(p) --> p0, p1, ... plast, p0, p1, ...
repeat(elem [,n]) --> elem, elem, elem, ... endlessly or up to n times

Iterators terminating on the shortest input sequence:
accumulate(p[, func]) --> p0, p0+p1, p0+p1+p2
batched(p, n) --> [p0, p1, ..., p_n-1], [p_n, p_n+1, ..., p_2n-1], ...
chain(p, q, ...) --> p0, p1, ... plast, q0, q1, ...
chain.from_iterable([p, q, ...]) --> p0, p1, ... plast, q0, q1, ...
compress(data, selectors) --> (d[0] if s[0]), (d[1] if s[1]), ...
dropwhile(predicate, seq) --> seq[n], seq[n+1], starting when predicate fails
groupby(iterable[, keyfunc]) --> sub-iterators grouped by value of keyfunc(v)
filterfalse(predicate, seq) --> elements of seq where predicate(elem) is False
islice(seq, [start,] stop [, step]) --> elements from
       seq[start:stop:step]
pairwise(s) --> (s[0],s[1]), (s[1],s[2]), (s[2], s[3]), ...
starmap(fun, seq) --> fun(*seq[0]), fun(*seq[1]), ...
tee(it, n=2) --> (it1, it2 , ... itn) splits one iterator into n
takewhile(predicate, seq) --> seq[0], seq[1], until predicate fails
zip_longest(p, q, ...) --> (p[0], q[0]), (p[1], q[1]), ...

Combinatoric generators:
product(p, q, ... [repeat=1]) --> cartesian product
permutations(p[, r])
combinations(p, r)
combinations_with_replacement(p, r)
"""

import operator as _operator

__all__ = [
    "accumulate", "batched", "chain", "combinations",
    "combinations_with_replacement", "compress", "count", "cycle",
    "dropwhile", "filterfalse", "groupby", "islice", "pairwise",
    "permutations", "product", "repeat", "starmap", "takewhile", "tee",
    "zip_longest",
]

_MARKER = object()


def _index(value, what):
    try:
        return _operator.index(value)
    except TypeError:
        raise TypeError(f"{what} must be an integer, not {type(value).__name__}") from None


# ---------------------------------------------------------------------------
# Infinite iterators
# ---------------------------------------------------------------------------

class count:
    """Return a count object whose .__next__() method returns consecutive values."""


    def __init__(self, start=0, step=1):
        for v in (start, step):
            if not isinstance(v, (int, float, complex)) and not hasattr(v, "__add__"):
                raise TypeError("a number is required")
        self._n = start
        self._step = step

    def __iter__(self):
        return self

    def __next__(self):
        n = self._n
        self._n = n + self._step
        return n

    def __repr__(self):
        if self._step == 1 and type(self._step) is int:
            return f"count({self._n!r})"
        return f"count({self._n!r}, {self._step!r})"


class cycle:
    """Return elements from the iterable until it is exhausted. Then repeat the sequence indefinitely."""


    def __init__(self, iterable, /):
        self._it = iter(iterable)
        self._saved = []
        self._index = 0
        self._first_pass = True

    def __iter__(self):
        return self

    def __next__(self):
        if self._first_pass:
            for element in self._it:
                self._saved.append(element)
                return element
            self._first_pass = False
            self._it = None
        saved = self._saved
        if not saved:
            raise StopIteration
        element = saved[self._index]
        self._index = (self._index + 1) % len(saved)
        return element


class repeat:
    """repeat(object [,times]) -> create an iterator which returns the object
    for the specified number of times.  If not specified, returns the object
    endlessly."""


    def __init__(self, object, times=None):
        self._object = object
        if times is None:
            self._times = None
        else:
            times = _index(times, "times")
            self._times = times if times > 0 else 0

    def __iter__(self):
        return self

    def __next__(self):
        times = self._times
        if times is None:
            return self._object
        if times <= 0:
            raise StopIteration
        self._times = times - 1
        return self._object

    def __length_hint__(self):
        if self._times is None:
            raise TypeError("len() of unsized object")
        return self._times

    def __repr__(self):
        if self._times is None:
            return f"repeat({self._object!r})"
        return f"repeat({self._object!r}, {self._times})"


# ---------------------------------------------------------------------------
# Iterators terminating on the shortest input sequence
# ---------------------------------------------------------------------------

class accumulate:
    """Return series of accumulated sums (or other binary function results)."""


    def __init__(self, iterable, func=None, *, initial=None):
        self._it = iter(iterable)
        self._func = func
        self._total = _MARKER
        self._initial = initial

    def __iter__(self):
        return self

    def __next__(self):
        if self._total is _MARKER:
            if self._initial is not None:
                self._total = self._initial
                self._initial = None
                return self._total
            self._total = next(self._it)
            return self._total
        element = next(self._it)
        func = self._func
        self._total = self._total + element if func is None else func(self._total, element)
        return self._total


class batched:
    """Batch data into tuples of length n. The last batch may be shorter than n."""


    def __init__(self, iterable, n, *, strict=False):
        n = _index(n, "n")
        if n < 1:
            raise ValueError("n must be at least one")
        self._it = iter(iterable)
        self._n = n
        self._strict = strict

    def __iter__(self):
        return self

    def __next__(self):
        it = self._it
        batch = []
        for _ in range(self._n):
            for element in it:
                batch.append(element)
                break
            else:
                break
        if not batch:
            raise StopIteration
        if self._strict and len(batch) != self._n:
            raise ValueError("batched(): incomplete batch")
        return tuple(batch)


class chain:
    """Return a chain object whose .__next__() method returns elements from the
    first iterable until it is exhausted, then elements from the next
    iterable, until all of the iterables are exhausted."""


    def __init__(self, *iterables):
        self._source = iter(iterables)
        self._active = None

    @classmethod
    def from_iterable(cls, iterable, /):
        """Alternative chain() constructor taking a single iterable argument
        that evaluates lazily."""
        self = cls.__new__(cls)
        self._source = iter(iterable)
        self._active = None
        return self

    def __iter__(self):
        return self

    def __next__(self):
        while True:
            active = self._active
            if active is None:
                source = self._source
                if source is None:
                    raise StopIteration
                for iterable in source:
                    active = self._active = iter(iterable)
                    break
                else:
                    self._source = None
                    raise StopIteration
            for element in active:
                return element
            self._active = None

    def __class_getitem__(cls, item):
        import types
        return types.GenericAlias(cls, item)


class compress:
    """Return data elements corresponding to true selector elements."""


    def __init__(self, data, selectors):
        self._data = iter(data)
        self._selectors = iter(selectors)

    def __iter__(self):
        return self

    def __next__(self):
        data = self._data
        selectors = self._selectors
        while True:
            element = next(data)
            if next(selectors):
                return element


class dropwhile:
    """Drop items from the iterable while predicate(item) is true."""


    def __init__(self, predicate, iterable, /):
        self._predicate = predicate
        self._it = iter(iterable)
        self._dropping = True

    def __iter__(self):
        return self

    def __next__(self):
        it = self._it
        if self._dropping:
            predicate = self._predicate
            for element in it:
                if not predicate(element):
                    self._dropping = False
                    return element
            raise StopIteration
        return next(it)


class filterfalse:
    """Return those items of iterable for which function(item) is false."""


    def __init__(self, function, iterable, /):
        self._predicate = bool if function is None else function
        self._it = iter(iterable)

    def __iter__(self):
        return self

    def __next__(self):
        predicate = self._predicate
        for element in self._it:
            if not predicate(element):
                return element
        raise StopIteration


class groupby:
    """make an iterator that returns consecutive keys and groups from the iterable"""


    def __init__(self, iterable, key=None):
        self._keyfunc = key
        self._it = iter(iterable)
        self._tgtkey = self._currkey = self._currvalue = _MARKER
        self._id = object()

    def __iter__(self):
        return self

    def __next__(self):
        self._id = object()
        while self._currkey is _MARKER or self._currkey == self._tgtkey:
            self._advance()
        self._tgtkey = self._currkey
        return (self._currkey, _grouper(self, self._tgtkey, self._id))

    def _advance(self):
        self._currvalue = next(self._it)
        keyfunc = self._keyfunc
        self._currkey = self._currvalue if keyfunc is None else keyfunc(self._currvalue)


class _grouper:

    def __init__(self, parent, tgtkey, id_):
        self._parent = parent
        self._tgtkey = tgtkey
        self._id = id_

    def __iter__(self):
        return self

    def __next__(self):
        parent = self._parent
        if parent._id is not self._id:
            raise StopIteration
        if parent._currkey is _MARKER:
            try:
                parent._advance()
            except StopIteration:
                parent._currkey = _MARKER
                parent._id = object()
                raise
        if parent._currkey != self._tgtkey:
            raise StopIteration
        value = parent._currvalue
        parent._currkey = _MARKER
        return value


class islice:
    """islice(iterable, stop) --> islice object
islice(iterable, start, stop[, step]) --> islice object

Return an iterator whose next() method returns selected values from an
iterable.  If start is specified, will skip all preceding elements;
otherwise, start defaults to zero.  Step defaults to one.  If
specified as another value, step determines how many values are
skipped between successive calls.  Works like a slice() on a list
but returns an iterator."""

    def __init__(self, iterable, *args):
        if not 1 <= len(args) <= 3:
            raise TypeError(f"islice expected at most 4 arguments, got {len(args) + 1}")
        s = slice(*args)
        start, stop, step = s.start, s.stop, s.step
        if stop is not None:
            try:
                stop = _operator.index(stop)
            except TypeError:
                stop = -1
            if stop < 0:
                raise ValueError("Stop argument for islice() must be None or an integer: "
                                 "0 <= x <= sys.maxsize.")
        if start is None:
            start = 0
        else:
            try:
                start = _operator.index(start)
            except TypeError:
                start = -1
            if start < 0:
                raise ValueError("Indices for islice() must be None or an integer: "
                                 "0 <= x <= sys.maxsize.")
        if step is None:
            step = 1
        else:
            try:
                step = _operator.index(step)
            except TypeError:
                step = -1
            if step < 1:
                raise ValueError("Step for islice() must be a positive integer or None.")
        self._it = iter(iterable)
        self._next = start      # position of the next element to return
        self._stop = stop
        self._step = step
        self._count = 0         # elements consumed from the source so far

    def __iter__(self):
        return self

    def __next__(self):
        it = self._it
        if it is None:
            raise StopIteration
        stop = self._stop
        try:
            while self._count < self._next:
                next(it)
                self._count += 1
            if stop is not None and self._count >= stop:
                raise StopIteration
            item = next(it)
        except StopIteration:
            self._it = None
            raise
        self._count += 1
        nxt = self._next + self._step
        if stop is not None and nxt > stop:
            nxt = stop
        self._next = nxt
        return item


class pairwise:
    """Return an iterator of overlapping pairs taken from the input iterator.

    s -> (s0,s1), (s1,s2), (s2, s3), ..."""

    def __init__(self, iterable, /):
        self._it = iter(iterable)
        self._last = _MARKER

    def __iter__(self):
        return self

    def __next__(self):
        it = self._it
        if it is None:
            raise StopIteration
        try:
            if self._last is _MARKER:
                self._last = next(it)
            new = next(it)
        except StopIteration:
            self._it = None
            raise
        result = (self._last, new)
        self._last = new
        return result


class starmap:
    """Return an iterator whose values are returned from the function
    evaluated with an argument tuple taken from the given sequence."""

    def __init__(self, function, iterable, /):
        self._function = function
        self._it = iter(iterable)

    def __iter__(self):
        return self

    def __next__(self):
        return self._function(*next(self._it))


class takewhile:
    """Return successive entries from an iterable as long as the predicate
    evaluates to true for each entry."""

    def __init__(self, predicate, iterable, /):
        self._predicate = predicate
        self._it = iter(iterable)

    def __iter__(self):
        return self

    def __next__(self):
        it = self._it
        if it is None:
            raise StopIteration
        element = next(it)
        if self._predicate(element):
            return element
        self._it = None
        raise StopIteration


class _tee_link:
    """One element of the buffer shared by the iterators of one tee()."""

    def __init__(self):
        self.value = _MARKER
        self.next = None


class _tee:
    """Iterator wrapped to make it copyable."""

    def __init__(self, source, link):
        self._source = source   # a one-element list holding the shared iterator
        self._link = link

    def __iter__(self):
        return self

    def __next__(self):
        link = self._link
        if link.next is None:
            source = self._source
            if source[0] is None:
                raise StopIteration
            try:
                value = next(source[0])
            except StopIteration:
                source[0] = None
                raise
            # Another copy may have filled this link while next() ran.
            if link.next is None:
                link.value = value
                link.next = _tee_link()
        self._link = link.next
        return link.value

    def __copy__(self):
        return _tee(self._source, self._link)


def tee(iterable, n=2, /):
    """Returns a tuple of n independent iterators."""
    n = _index(n, "n")
    if n < 0:
        raise ValueError("n must be >= 0")
    if n == 0:
        return ()
    it = iter(iterable)
    if isinstance(it, _tee):
        first = it
    else:
        first = _tee([it], _tee_link())
    return (first,) + tuple(first.__copy__() for _ in range(n - 1))


class zip_longest:
    """Return a zip_longest object whose .__next__() method returns a tuple
    where the i-th element comes from the i-th iterable argument; missing
    values are filled in with fillvalue."""

    def __init__(self, *iterables, fillvalue=None):
        self._its = [iter(it) for it in iterables]
        self._active = len(self._its)
        self._fillvalue = fillvalue

    def __iter__(self):
        return self

    def __next__(self):
        its = self._its
        if not its or self._active == 0:
            raise StopIteration
        values = []
        for i, it in enumerate(its):
            if it is None:
                values.append(self._fillvalue)
                continue
            try:
                values.append(next(it))
            except StopIteration:
                self._active -= 1
                if self._active == 0:
                    self._its = []
                    raise
                its[i] = None
                values.append(self._fillvalue)
        return tuple(values)


# ---------------------------------------------------------------------------
# Combinatoric iterators
# ---------------------------------------------------------------------------

class _combinatoric:
    """Base of the combinatoric iterators: __next__ drives a generator."""

    def __iter__(self):
        return self

    def __next__(self):
        return next(self._gen)


class product(_combinatoric):
    """Cartesian product of input iterables.  Equivalent to nested for-loops."""

    def __init__(self, *iterables, repeat=1):
        repeat = _index(repeat, "repeat")
        if repeat < 0:
            raise ValueError("repeat argument cannot be negative")
        pools = [tuple(pool) for pool in iterables] * repeat
        self._gen = self._run(pools)

    @staticmethod
    def _run(pools):
        if any(not pool for pool in pools):
            return
        n = len(pools)
        indices = [0] * n
        yield tuple(pool[0] for pool in pools)
        while True:
            for i in reversed(range(n)):
                indices[i] += 1
                if indices[i] < len(pools[i]):
                    break
                indices[i] = 0
            else:
                return
            yield tuple(pool[j] for pool, j in zip(pools, indices))


class permutations(_combinatoric):
    """Return successive r-length permutations of elements in the iterable."""

    def __init__(self, iterable, r=None):
        pool = tuple(iterable)
        n = len(pool)
        if r is None:
            r = n
        else:
            r = _index(r, "r")
            if r < 0:
                raise ValueError("r must be non-negative")
        self._gen = self._run(pool, n, r)

    @staticmethod
    def _run(pool, n, r):
        if r > n:
            return
        indices = list(range(n))
        cycles = list(range(n, n - r, -1))
        yield tuple(pool[i] for i in indices[:r])
        while n:
            for i in reversed(range(r)):
                cycles[i] -= 1
                if cycles[i] == 0:
                    indices[i:] = indices[i + 1:] + indices[i:i + 1]
                    cycles[i] = n - i
                else:
                    j = cycles[i]
                    indices[i], indices[-j] = indices[-j], indices[i]
                    yield tuple(pool[i] for i in indices[:r])
                    break
            else:
                return


class combinations(_combinatoric):
    """Return successive r-length combinations of elements in the iterable."""

    def __init__(self, iterable, r):
        r = _index(r, "r")
        if r < 0:
            raise ValueError("r must be non-negative")
        self._gen = self._run(tuple(iterable), r)

    @staticmethod
    def _run(pool, r):
        n = len(pool)
        if r > n:
            return
        indices = list(range(r))
        yield tuple(pool[i] for i in indices)
        while True:
            for i in reversed(range(r)):
                if indices[i] != i + n - r:
                    break
            else:
                return
            indices[i] += 1
            for j in range(i + 1, r):
                indices[j] = indices[j - 1] + 1
            yield tuple(pool[i] for i in indices)


class combinations_with_replacement(_combinatoric):
    """Return successive r-length combinations of elements in the iterable
    allowing individual elements to have successive repeats."""

    def __init__(self, iterable, r):
        r = _index(r, "r")
        if r < 0:
            raise ValueError("r must be non-negative")
        self._gen = self._run(tuple(iterable), r)

    @staticmethod
    def _run(pool, r):
        n = len(pool)
        if not n and r:
            return
        indices = [0] * r
        yield tuple(pool[i] for i in indices)
        while True:
            for i in reversed(range(r)):
                if indices[i] != n - 1:
                    break
            else:
                return
            indices[i:] = [indices[i] + 1] * (r - i)
            yield tuple(pool[i] for i in indices)
