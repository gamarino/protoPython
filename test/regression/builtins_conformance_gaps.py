# Pre-existing conformance gaps fixed in 1.2.2 (review rounds 4 and 6).
#
# Each section is compiled and run on its own, so a section fails alone:
# on a build without one fix the others still pass (the subnormal literals,
# for instance, are a SyntaxError on a build without the tokenizer fix).

SECTIONS = {}
SECTIONS['pow_inverse'] = r'''
from fractions import Fraction
# pow() modular inverse, and the numeric tower's hash through it.
assert pow(3, -1, 7) == 5
assert pow(38, -1, 97) == 23
assert pow(2, -2, 7) == 2
assert pow(-3, -1, 7) == 2
try:
    pow(2, -1, 4)
except ValueError:
    pass
else:
    raise AssertionError("pow(2, -1, 4) did not raise")
assert hash(Fraction(3, 1)) == 3
assert hash(Fraction(1, 2)) == hash(0.5)
assert hash(Fraction(-7, 3)) == hash(Fraction(7, 3)) * -1 or hash(Fraction(-7, 3)) == -hash(Fraction(7, 3))
assert {Fraction(3): 'x'}.get(3) == 'x' and {3: 'y'}.get(Fraction(3)) == 'y'
assert len({Fraction(1, 2), 0.5}) == 1
'''
SECTIONS['dict_ior'] = r'''
# d |= iterable of pairs
d = {1: 0}
d |= [(1, 2), (3, 4)]
assert d == {1: 2, 3: 4}
d |= ((5, 6),)
assert d[5] == 6
try:
    d |= 5
except TypeError:
    pass
else:
    raise AssertionError("d |= 5 did not raise")
'''
SECTIONS['deque_index'] = r'''
from collections import deque
# deque.index
q = deque([1, 2, 3, 2])
assert q.index(2) == 1
assert q.index(2, 2) == 3
assert q.index(3, 0, 3) == 2
try:
    q.index(9)
except ValueError:
    pass
else:
    raise AssertionError("deque.index of a missing item did not raise")
'''
SECTIONS['slice_assign_iter'] = r'''
# slice assignment from an iterator
l = [1, 2, 3, 4]
l[1:3] = (x for x in 'ab')
assert l == [1, 'a', 'b', 4]
l[::2] = iter([7, 8])
assert l == [7, 'a', 8, 4]
l[0:0] = map(str, (1,))
assert l == ['1', 7, 'a', 8, 4]
'''
SECTIONS['subnormal_floats'] = r'''
# subnormal floats
assert 1e-320 > 0 and 1e-320 == float('1e-320')
assert 5e-324 == float('5e-324') and float('4e-324') == 5e-324
assert -1e-310 < 0
assert float('1e-400') == 0.0
'''
SECTIONS['stack_budget'] = r'''
# a list literal of many floats; the frame's operand stack is sized at
# compile time, and every call through a name used to be counted one slot
# short, so the calls before the literal shrank the budget.
def ident(v):
    return v
calls = [ident(1), ident(2), ident(3), ident(4), ident(5), ident(6), ident(7), ident(8)]
calls = [ident(1), ident(2), ident(3), ident(4), ident(5), ident(6), ident(7), ident(8)]
calls = [ident(1), ident(2), ident(3), ident(4), ident(5), ident(6), ident(7), ident(8)]
calls = [ident(1), ident(2), ident(3), ident(4), ident(5), ident(6), ident(7), ident(8)]
xs = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5, 10.5, 11.5, 12.5, 13.5, 14.5,
      15.5, 16.5, 17.5, 18.5, 19.5, 20.5, 21.5, 22.5, 23.5, 24.5, 25.5, 26.5, 27.5,
      28.5, 29.5, 30.5, 31.5, 32.5, 33.5, 34.5, 35.5, 36.5, 37.5, 38.5, 39.5, 40.5]
assert len(xs) == 41 and sum(xs) == 840.5
ts = (1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0,
      17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0, 28.0, 29.0, 30.0, 31.0, 32.0)
assert len(ts) == 32
ss = {1.25, 2.25, 3.25, 4.25, 5.25, 6.25, 7.25, 8.25, 9.25, 10.25, 11.25, 12.25, 13.25, 14.25, 15.25,
      16.25, 17.25, 18.25, 19.25, 20.25, 21.25, 22.25, 23.25, 24.25, 25.25, 26.25, 27.25, 28.25, 29.25, 30.25, 31.25}
assert len(ss) == 31
'''
SECTIONS['set_subclass_repr'] = r'''
# repr of a frozenset subclass
class F(frozenset):
    pass
assert repr(F({1})) == "F({1})"
assert repr(F()) == "F()"
class S(set):
    pass
assert repr(S({1})) == "S({1})"
assert repr(frozenset({1})) == "frozenset({1})"
'''
SECTIONS['pow_modulus_sign'] = r'''
# pow(base, exp, mod) has the sign of the modulus, takes any exponent and
# refuses floats.
assert pow(3, 2, -7) == -5
assert pow(-3, 2, 7) == 2
assert pow(3, 0, 1) == 0 and pow(3, 0, -1) == 0
assert pow(3, -1, -7) == -2
assert pow(3, -(10**20), 10**9 + 7) == 85797096
assert pow(3, 10**20, 10**9 + 7) == pow(pow(3, -(10**20), 10**9 + 7), -1, 10**9 + 7)
assert pow(2, 3, None) == 8
for args in ((2.0, -1, 3), (2, 3.0, 5), (2, 3, 5.0)):
    try:
        pow(*args)
    except TypeError as e:
        assert "3rd argument not allowed" in str(e), e
    else:
        raise AssertionError("pow%r did not raise" % (args,))
'''

SECTIONS['complex_repr'] = r'''
assert repr(1e-320j) == "1e-320j"
assert repr(0.1 + 0.2j) == "(0.1+0.2j)"
assert repr(complex(1, -0.0)) == "(1-0j)"
assert repr(complex(-0.0, 1)) == "(-0+1j)"
assert repr(complex(0.0, -0.0)) == "-0j"
assert repr(1e20j) == "1e+20j" and repr(1e15j) == "1000000000000000j"
assert repr(complex(float('nan'), float('inf'))) == "(nan+infj)"
assert str(2.5 - 1j) == "(2.5-1j)"
# complex's reflected operators: a real number on the left.
assert 2 - 1j == complex(2, -1) and 2.5 * 1j == 2.5j and 2.5 / 1j == -2.5j
assert abs(2 ** 1j - complex(0.7692389013639721, 0.6389612763136348)) < 1e-12
assert complex.__rsub__(1j, 2.5) == complex(2.5, -1)
assert (1j).__rmul__('x') is NotImplemented
'''

failed = []
for name, code in SECTIONS.items():
    try:
        exec(compile(code, "<%s>" % name, "exec"), {"__name__": "__section__"})
    except BaseException as e:
        failed.append(name)
        print("section %s: FAIL %s: %s" % (name, type(e).__name__, e))
if failed:
    print("builtins_conformance_gaps: failed", failed)
else:
    print("builtins_conformance_gaps: ok")
