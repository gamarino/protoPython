"""Tests for match/case (PEP 634) under protoPython.

Covers MatchValue, MatchSingleton, MatchSequence (with star), MatchMapping,
MatchClass (with positional + keyword sub-patterns), MatchAs, MatchOr,
wildcard, and guards.
"""
import unittest


class MatchValueTests(unittest.TestCase):
    def test_int_literal(self):
        def f(x):
            match x:
                case 1: return 'one'
                case 2: return 'two'
                case _: return 'other'
        self.assertEqual(f(1), 'one')
        self.assertEqual(f(2), 'two')
        self.assertEqual(f(99), 'other')

    def test_string_literal(self):
        def f(x):
            match x:
                case 'a': return 1
                case 'b': return 2
                case _: return 0
        self.assertEqual(f('a'), 1)
        self.assertEqual(f('b'), 2)
        self.assertEqual(f('c'), 0)

    def test_signed_literal(self):
        def f(x):
            match x:
                case -1: return 'neg-one'
                case 0: return 'zero'
                case _: return 'other'
        self.assertEqual(f(-1), 'neg-one')
        self.assertEqual(f(0), 'zero')
        self.assertEqual(f(5), 'other')


class MatchSingletonTests(unittest.TestCase):
    def test_none_true_false(self):
        def f(x):
            match x:
                case None: return 'N'
                case True: return 'T'
                case False: return 'F'
                case _: return 'o'
        self.assertEqual(f(None), 'N')
        self.assertEqual(f(True), 'T')
        self.assertEqual(f(False), 'F')

    def test_singleton_uses_is(self):
        # PEP 634: singleton patterns use `is`, not `==`.
        # 0 == False but `0 is False` is False.
        def f(x):
            match x:
                case False: return 'F'
                case _: return 'other'
        self.assertEqual(f(0), 'other')


class MatchAsTests(unittest.TestCase):
    def test_capture(self):
        def f(x):
            match x:
                case n: return ('cap', n)
        self.assertEqual(f(7), ('cap', 7))
        self.assertEqual(f('hi'), ('cap', 'hi'))

    def test_wildcard(self):
        def f(x):
            match x:
                case _: return 'wild'
        self.assertEqual(f(1), 'wild')
        self.assertEqual(f(None), 'wild')

    def test_as_with_subpattern(self):
        def f(x):
            match x:
                case [a, b] as pair: return ('pair', pair, a + b)
                case _: return 'no'
        self.assertEqual(f([3, 4]), ('pair', [3, 4], 7))
        self.assertEqual(f(99), 'no')


class MatchOrTests(unittest.TestCase):
    def test_or_simple(self):
        def f(x):
            match x:
                case 1 | 2 | 3: return 'small'
                case _: return 'other'
        for v in (1, 2, 3):
            self.assertEqual(f(v), 'small')
        self.assertEqual(f(4), 'other')

    def test_or_strings(self):
        def f(x):
            match x:
                case 'a' | 'b' | 'c': return 'abc'
                case _: return 'other'
        for v in ('a', 'b', 'c'):
            self.assertEqual(f(v), 'abc')
        self.assertEqual(f('d'), 'other')

    def test_or_with_capture(self):
        # All alternatives bind the same name.
        def f(x):
            match x:
                case [a] | (a,): return a
                case _: return None
        self.assertEqual(f([7]), 7)
        self.assertEqual(f((9,)), 9)


class MatchClassTests(unittest.TestCase):
    def test_class_no_args(self):
        def f(x):
            match x:
                case int(): return 'int'
                case str(): return 'str'
                case list(): return 'list'
                case _: return 'other'
        self.assertEqual(f(7), 'int')
        self.assertEqual(f('hi'), 'str')
        self.assertEqual(f([1]), 'list')
        self.assertEqual(f(3.14), 'other')

    def test_class_with_match_args(self):
        self.assertEqual(_match_pt(_Pt(0, 0)), 'origin')
        self.assertEqual(_match_pt(_Pt(5, 0)), ('xax', 5))
        self.assertEqual(_match_pt(_Pt(0, 9)), ('yax', 9))
        self.assertEqual(_match_pt(_Pt(3, 4)), ('pt', 3, 4))
        self.assertEqual(_match_pt(42), 'no')


class _Pt:
    """Module-level helper for class-pattern tests (avoids a closure
    issue with classes defined inside test methods + match-statement)."""
    __match_args__ = ('x', 'y')
    def __init__(self, x, y):
        self.x = x
        self.y = y


def _match_pt(p):
    match p:
        case _Pt(0, 0): return 'origin'
        case _Pt(x, 0): return ('xax', x)
        case _Pt(0, y): return ('yax', y)
        case _Pt(x=a, y=b): return ('pt', a, b)
        case _: return 'no'


class MatchSequenceTests(unittest.TestCase):
    def test_fixed_length(self):
        def f(x):
            match x:
                case []: return 'empty'
                case [a]: return ('one', a)
                case [a, b]: return ('two', a, b)
                case _: return 'other'
        self.assertEqual(f([]), 'empty')
        self.assertEqual(f([1]), ('one', 1))
        self.assertEqual(f([1, 2]), ('two', 1, 2))
        self.assertEqual(f((9, 10)), ('two', 9, 10))
        self.assertEqual(f([1, 2, 3]), 'other')

    def test_star_at_end(self):
        def f(x):
            match x:
                case [a, *rest]: return (a, rest)
                case _: return 'other'
        self.assertEqual(f([1, 2, 3, 4]), (1, [2, 3, 4]))
        self.assertEqual(f([5]), (5, []))

    def test_star_in_middle(self):
        def f(x):
            match x:
                case [a, *m, z]: return (a, m, z)
                case _: return 'other'
        self.assertEqual(f([1, 2, 3, 4, 5]), (1, [2, 3, 4], 5))
        self.assertEqual(f([10, 20]), (10, [], 20))

    def test_strings_rejected(self):
        # PEP 634: strings/bytes/bytearray are NOT sequences for matching.
        def f(x):
            match x:
                case [a, *_]: return 'seq'
                case _: return 'other'
        self.assertEqual(f('hi'), 'other')
        self.assertEqual(f(b'bb'), 'other')


class MatchMappingTests(unittest.TestCase):
    def test_required_keys(self):
        def f(x):
            match x:
                case {'a': v}: return ('a', v)
                case {'b': v1, 'c': v2}: return ('bc', v1, v2)
                case _: return 'other'
        self.assertEqual(f({'a': 7}), ('a', 7))
        self.assertEqual(f({'a': 7, 'extra': 9}), ('a', 7))
        self.assertEqual(f({'b': 1, 'c': 2}), ('bc', 1, 2))
        self.assertEqual(f({'x': 1}), 'other')

    def test_empty_mapping_matches_any_dict(self):
        def f(x):
            match x:
                case {}: return 'any-dict'
                case _: return 'other'
        self.assertEqual(f({}), 'any-dict')
        self.assertEqual(f({'k': 'v'}), 'any-dict')
        self.assertEqual(f([1]), 'other')

    def test_rest_binding(self):
        def f(x):
            match x:
                case {'a': v, **rest}: return (v, rest)
                case _: return 'other'
        self.assertEqual(f({'a': 1, 'b': 2, 'c': 3}), (1, {'b': 2, 'c': 3}))
        self.assertEqual(f({'a': 5}), (5, {}))


class MatchGuardTests(unittest.TestCase):
    def test_guard(self):
        def f(n):
            match n:
                case x if x < 0: return 'neg'
                case 0: return 'zero'
                case x if x > 100: return 'big'
                case _: return 'mid'
        self.assertEqual(f(-1), 'neg')
        self.assertEqual(f(0), 'zero')
        self.assertEqual(f(200), 'big')
        self.assertEqual(f(50), 'mid')


class MatchNestedTests(unittest.TestCase):
    def test_nested_mapping(self):
        def f(x):
            match x:
                case {'kind': 'sum', 'left': l, 'right': r}: return ('sum', l, r)
                case {'kind': 'lit', 'value': v}: return ('lit', v)
                case _: return 'other'
        self.assertEqual(
            f({'kind': 'sum', 'left': 1, 'right': 2}),
            ('sum', 1, 2))
        self.assertEqual(
            f({'kind': 'lit', 'value': 42}),
            ('lit', 42))


if __name__ == '__main__':
    unittest.main()
