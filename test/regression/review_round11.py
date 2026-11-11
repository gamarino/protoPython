# Review round 11 of 1.2.2 (tasks/p8-round10-review-report.md, ruling Q21
# amended): a spec-less field of an int, float or str subclass is str(value)
# (H1, a round-10 regression for f-strings), Enum members with a mixed-in
# data type format and print as CPython's enum module makes them, a held
# instance __dict__ no longer writes stale values back (H2), and
# math.sumprod accumulates in extended precision as CPython 3.12+ (M2).
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix.

SECTIONS = {}

SECTIONS['H1_subclass_str'] = r"""
class BS(int):
    def __str__(self): return 'BSstr'
class IR(int):
    def __repr__(self): return 'IRrepr'
class FStr(float):
    def __str__(self): return 'FStr'
class FR(float):
    def __repr__(self): return 'FRrepr'
class SS(str):
    def __str__(self): return 'SSstr'
for v, want in [(BS(3), 'BSstr'), (IR(3), 'IRrepr'), (FStr(2.5), 'FStr'), (FR(2.5), 'FRrepr'), (SS('a'), 'SSstr')]:
    got = [str(v), f'{v}', f'{v!s}', format(v), format(v, ''), '{}'.format(v), '{a}'.format_map({'a': v}), '%s' % (v,)]
    assert got == [want] * len(got), (want, got)
# A spec still formats the value, and errors name the subclass.
assert f'{BS(3):>4}' == '   3' and f'{FStr(2.5):.2f}' == '2.50' and f'{SS("a"):>3}' == '  a'
for v, tn in [(BS(3), 'BS'), (FStr(2.5), 'FStr'), (True, 'bool'), (5, 'int'), (1.5, 'float')]:
    try:
        format(v, 'q')
    except ValueError as e:
        assert str(e) == f"Unknown format code 'q' for object of type '{tn}'", str(e)
    else:
        raise AssertionError(v)
try:
    format('a', ' ')
except ValueError as e:
    assert str(e) == 'Space not allowed in string format specifier', str(e)
# A __str__ or __repr__ given to a class (or a base) later counts too.
class Late(int): pass
class LateBase: pass
class Late2(LateBase, float): pass
assert f'{Late(4)}' == '4' and str(Late2(1.5)) == '1.5'
Late.__str__ = lambda s: 'late'
LateBase.__repr__ = lambda s: 'base'
assert f'{Late(4)}' == 'late' and format(Late(4)) == 'late' and str(Late2(1.5)) == 'base' and f'{Late2(1.5)}' == 'base'
assert f'{Late(4):>3}' == '  4'
assert f'{True}' == 'True' and format(False) == 'False' and f'{True:>5}' == '    1' and '%s' % True == 'True'
"""

SECTIONS['H1_enum_mixins'] = r"""
import enum
class Color(enum.IntEnum):
    R = 1
class SE(str, enum.Enum):
    X = 'x'
class SE2(enum.StrEnum):
    Y = 'y'
class FE(float, enum.Enum):
    P = 1.5
class IF(enum.IntFlag):
    A = 1
    B = 2
rows = []
for v in [Color.R, SE.X, SE2.Y, FE.P, IF.A, IF.A | IF.B]:
    rows.append((repr(v), str(v), f'{v}', f'{v!r}', format(v, '>6'), '{}'.format(v), '%s' % (v,), '%r' % (v,)))
want = [
    ('<Color.R: 1>', '1', '1', '<Color.R: 1>', '     1', '1', '1', '<Color.R: 1>'),
    ("<SE.X: 'x'>", 'SE.X', 'SE.X', "<SE.X: 'x'>", '  SE.X', 'SE.X', 'SE.X', "<SE.X: 'x'>"),
    ("<SE2.Y: 'y'>", 'y', 'y', "<SE2.Y: 'y'>", '     y', 'y', 'y', "<SE2.Y: 'y'>"),
    ('<FE.P: 1.5>', 'FE.P', 'FE.P', '<FE.P: 1.5>', '  FE.P', 'FE.P', 'FE.P', '<FE.P: 1.5>'),
    ('<IF.A: 1>', '1', '1', '<IF.A: 1>', '     1', '1', '1', '<IF.A: 1>'),
    ('<IF.A|B: 3>', '3', '3', '<IF.A|B: 3>', '     3', '3', '3', '<IF.A|B: 3>'),
]
for r, w in zip(rows, want):
    assert r == w, (r, w)
"""

SECTIONS['H2_held_instance_dict'] = r"""
class C:
    pass
o = C()
o.x = 0
d = o.__dict__
o.x = 1
d.update({'m': 6})
assert o.x == 1 and o.m == 6, vars(o)
d.update(n=7)
assert o.x == 1 and o.n == 7, vars(o)
o.x = 2
d |= {'r': 2}
assert o.x == 2 and o.r == 2, vars(o)
# Mutators act on the instance's current namespace.
o.y = 5
assert d.setdefault('y', 0) == 5 and o.y == 5
o.z = 9
assert d.pop('z') == 9 and not hasattr(o, 'z')
o.w = 3
del d['w']
assert not hasattr(o, 'w')
o.p = 4
assert d.popitem() == ('p', 4) and not hasattr(o, 'p')
o.s = 1
d.clear()
assert vars(o) == {} and not hasattr(o, 's') and not hasattr(o, 'x')
# A fresh view and the attribute writes still agree.
o.a = 1
vars(o).update(b=2)
o.__dict__['c'] = 3
assert vars(o) == {'a': 1, 'b': 2, 'c': 3}, vars(o)
# The same through a function's namespace.
def f(): pass
fd = f.__dict__
f.k = 1
fd.update(j=2)
assert f.k == 1 and f.j == 2
"""

SUMPROD_EXPECTED = ['0x1.e714da5d4f7c6p+103', '0x1.87ca39dd1ce6ap+174', '0x1.10952e80de514p+94', '0x1.05b57257fe23cp+173', '-0x1.24e9f1088d58bp+135', '-0x1.61faf63dc0a72p+160', '0x1.3a763c05ead23p+158', '0x1.f1d0de5eab80ap+156', '0x1.3b2ecc7909e30p+162', '-0x1.16b4c2b4dcedfp+161', '0x1.29b3e836cb88dp+192', '-0x1.9d8fe3261c680p+110', '0', '0x1.0969dddd6f4bep+175', '0x1.61a2cd6456b41p+203', '-0x1.9cf5b1bd467a1p+143', '-0x1.b80afb2c1ca53p+155', '0x1.50b449fde6806p+159', '-0x1.05471c18b34b5p+200', '0x1.330711737e270p+183', '0x1.1cd0ef693ae27p+180', '-0x1.8a423efaf75a1p+195', '-0x1.8c406e44aa361p+160', '0x1.772ceb6967d8cp+219', '-0x1.2e06e94a7b4dcp+155', '-0x1.9bd466594330bp+167', '-0x1.527721fea8d22p+171', '-0x1.a408fe2dc12bfp+184', '-0x1.7ea1840299d5bp+134', '-0x1.109f45ad28f93p+82', '0x1.811ad2cb363d0p+159', '-0x1.953d0ea8d19bfp+161', '0x1.68ebeb229db47p+198', '0x1.ce3c4cefc00d3p+169', '0x1.cb67c9171310dp+130', '0x1.b7d34a85a9838p+189', '0x1.8da767ec047c8p+179', '0x1.7618b4113baf0p+187', '0x1.92d3e6b6e31edp+198', '0x1.0ba7c92cdad17p+196', '-0x1.96a2c5b2df3e3p+97', '-0x1.dfc22b63a4b14p+154', '-0x1.0de971767d688p+124', '0x1.ab6a718963889p+111', '0x1.85c9c9c8612e9p+148', '-0x1.7fdd9422220fep+173', '0x1.1692208e8f435p+155', '0x1.c4ae3fe2a1501p+173', '0x1.548902b92fcdfp+203', '-0x1.88f8dfa2a5d9bp+179', '-0x1.4ec3469bb995cp+135', '-0x1.194b0b765f69fp+162', '-0x1.0014daa0ccde3p+40', '0x1.24cf6fbd8a65fp+154', '0x1.e4a7851e1a5b5p+193', '0x1.cdc938d8acb67p+142', '-0x1.fe5a4b0998cd9p+112', '0x1.c403d44efcdfcp+144', '-0x1.77387c3936fa3p+79', '-0x1.7c074f3d7262ep+201', '-0x1.1b2989a378b19p+145', '-0x1.678b093d476eep+110', '-0x1.0aa31c37be5abp+205', '0x1.67e6a7b509173p+90', '0x1.b0af84d09c786p+186', '-0x1.76ec8a4678781p+201', '0x1.1c4783fe0e6fap+184', '0x1.46204db7e3cc1p+194', '-0x1.843c316ddab19p+7', '-0x1.d8ef4d23e313ap+159', '0x1.c882f9a7b5af5p+116', '-0x1.439c14f09f4dep+176', '-0x1.88153826a8453p+188', '0x1.e5c79fa9c1b2dp+128', '0x1.3d79b12046a5dp+168', '-0x1.518a34bdf0578p+177', '-0x1.20c8e5b6214a3p+178', '0x1.084e58bf45a1ep+185', '0x1.64ab919357093p+187', '0x1.a3171c775336bp+118', '-0x1.d92969916fe40p+131', '0x1.36fbf2479f20ep+206', '0x1.8e654b0d94b37p+127', '-0x1.7538bc5982664p+131', '0x1.c0e70e1143a61p+143', '0x1.bfcc41c2d152dp+151', '0x1.394ad0d4eee9bp+183', '-0x1.166b07cafdb93p+82', '0x1.e873226bf6d26p+180', '0x1.ecb5c7e52d3e3p+136', '0x1.b15cd9728c342p+140', '0x1.32c4e4f69fc03p+21', '-0x1.5a3fcd25134efp+159', '0x1.255361d631e2fp+112', '0x1.21a557f8371b0p+161', '0x1.7a3d49987de9bp+186', '0x1.155f6eefb8180p+64', '0x1.4e69e0cfbca10p+167', '-0x1.85422ad2d48cap+130', '-0x1.65a4724912bdep+122', '-0x1.9420ab0073f2fp+106', '0x1.a71e9a2b7e9c6p+75', '0x1.14e6ad1be2d27p+123', '0x1.7e6f2f7657118p+134', '0x1.3b54f5946462cp+160', '0x0.0p+0', '-0x1.ad1571a2ab714p+186', '0x1.9c377c7e66fc6p+137', '-0x1.5ebc0f0ab6a1ap+157', '0x1.16131a110a28bp+37', '-0x1.10e8b609ef214p+173', '0x1.c3fecd28274b0p+146', '-0x1.e0a3849d30bd7p+189', '-0x1.47126a35ede65p+207', '-0x1.e76e851bdfa64p+118', '0x1.e2ccc778c0ee7p+205', '-0x1.c3467db3632c7p+150', '0x1.4f237a3697ca6p+132', '-0x1.13df1d0b7024ap+204', '0x1.ca39f2d27ef9dp+116', '-0x1.9ec2b001d34acp+201', '-0x1.b655cbad46f1dp+161', '-0x1.79382a375f47cp+147', '0x1.413057c4fc8c7p+160', '-0x1.1e9b566e847b4p+153', '-0x1.8a92a80ca796fp+144', '-0x1.27b4a24169477p+170', '-0x1.aa396b4b3a0efp+212', '-0x1.065267d8a7915p+121', '-0x1.a67ff7deb07b3p+159', '0x1.01190aaa75418p+144', '-0x1.34f040df49185p+168', '0x1.4fd52b1f92ac7p+166', '-0x1.0f8805ae51648p+152', '-0x1.2a091e0f72647p+78', '-0x1.e657c3eaff303p+196', '0x1.eaff5360b68cfp+196', '-0x1.855379c976bafp+146', '0x1.abc28371d857cp+222', '-0x1.71ed8680033e3p+176', '-0x1.47d9ad1aaab29p+214', '-0x1.cca019f83ddd2p+117', '-0x1.0d55bcc29a4a5p+99', '0x1.39403eb9ee483p+188', '0x1.c149081818492p+115', '-0x1.e99861a8eece0p+143', '0x1.556b03a40c8e1p+169', '-0x1.ad25e44ba719ep+149', '0x1.31eb802676191p+150', '0x1.7ffe31c73f305p+193', '0x1.1461e139a03b3p+176', '-0x1.f3ab964c89adbp+123', '0x1.87d487d6dc567p+199', '0x1.15360a660c2f5p+150', '-0x1.fd51572fc39b4p+177', '-0x1.62a7fa30d04f7p+139', '-0x1.ed6bb3419a65ep+161', '-0x1.50362432822c0p+122', '0x1.b92faee425855p+147', '-0x1.55d18cab98d7fp+151', '0x1.0509fc77b96bap+162', '-0x1.8d148c08221fcp+195', '-0x1.a064800004c48p+150', '0x1.6ae2944dc21fap+169', '0x1.3069f7600ccd8p+152', '-0x1.517c52e0525b9p+213', '0x1.04325537044d4p+158', '0x1.ba38b578443efp+148', '-0x1.aa95258449e71p+146', '-0x1.04e93de4eb6dfp+188', '-0x1.5de2724c82864p+159', '-0x1.64ca8628a54adp+152', '-0x1.9e1472173e499p+41', '-0x1.7b83224daab42p+92', '-0x1.100acb10039cdp+55', '0x1.690cd777e18c6p+206', '-0x1.a14a2d06fe5e3p+212', '-0x1.7b555ab8f84f3p+113', '0x1.e0965aeec1aa7p+169', '-0x1.a243d045d0311p+164', '-0x1.0abbad0f43f9fp+160', '0x1.52b287a1e2c91p+153', '-0x1.acd71e224bf22p+128', '-0x1.25f8df1690955p+171', '0x1.5d69e15a8bea3p+90', '0x1.ede39bde7a16ap+195', '0x1.018622fffe648p+157', '0x1.19a2c04190f09p+136', '-0x1.3a65d14cd6c29p+170', '0x1.80443320f50c9p+194', '0x1.9af57db2e9b7dp+199', '0x1.8e58a9a4df4cep+143', '-0x1.2fe91571e2f73p+143', '0x1.f45900e78f580p+179', '-0x1.77f7df6fd8764p+99', '0x1.0639db0724563p+146', '0x1.577a223cfe79fp+169', '0x1.358a915bf5438p+146', '0x1.e8795fdb4e526p+106', '0x1.1f2a5c33f1abfp+156', '-0x1.491becc70a0e6p+110', '-0x1.ff4e370764639p+179', '-0x1.4ac368796d9cdp+144', '-0x1.45567826b96e5p+206', '-0x1.3593ed768656ap+188', '-0x1.df7fb8d2d9401p+123', '-0x1.65a9897351b32p+148', '0x1.9662d9cc38a72p+199', '-0x1.6c467945945e9p+126', '-0x1.bbc6e956846f7p+160', '-0x1.cf543e897d622p+184', '0x1.cf34ea54277fdp+147', '-0x1.b52a99a282e37p+155', '0x1.054d63f48c882p+211', '0x1.8000354924011p+158', '-0x1.a0119eb419cacp+150', '0x1.7c76cfe8732a1p+210', '0x1.ba903c609be12p+74', '-0x1.3d31ef5eea372p+184', '-0x1.7645434df28bbp+167', '-0x1.6041f52dd7871p+135', '-0x1.d3fc045f26286p+173', '-0x1.ff950e3ec13a0p+186', '-0x1.29739a69ac3f4p+89', '0x1.548025a080f39p+148', '-0x1.c26f56b7b2915p+187', '-0x1.ca3defcb60fcdp+115', '-0x1.eea190763cebdp+13', '0x1.cb44934bb9577p+173', '-0x1.815dc7a8d47d0p+187', '-0x1.785d1b86fbcc5p+131', '0x1.da4c363a8eb5ap+177', '0x1.9b48229555f00p+120', '0x1.24a66e807303dp+199', '-0x1.7641ed18c5b28p+87', '0x1.46d514cad769fp+175', '-0x1.c19689732de4cp+144', '-0x1.d5291d8fd3fa0p+193', '0x1.38ed86f53e81cp+114', '0x1.4ca7c3f12cfd5p+95', '0x1.9d134b470a76fp+166', '-0x1.3520387b1a995p+159', '-0x1.458dc35e4c96ep+164', '-0x1.2f2de6de38091p+137', '-0x1.087d291f5b7cdp+126', '-0x1.7a3e675fd1792p+183', '0x1.3d5d3e0249d75p+116', '-0x1.d58fd03687547p+193', '-0x1.24b127396122ep+181', '0x1.76b40ff18b99fp+92', '0x1.9eda789eb306bp+164', '0x1.168024f61466ep+116', '0x1.b2a395a0945b2p+164', '0x1.e5292e0537464p+216', '-0x1.603176255987ap+147', '-0x1.060705e718dbfp+211', '-0x1.e27b39b67c0b9p+189', '-0x1.05f0f1b1784b5p+209', '0x1.a85eb2637bc7ep+146', '0x1.5a53ce7c84d59p+203', '-0x1.664c0ff4f0940p+111', '-0x1.8b6f69630d59fp+189', '-0x1.6aa5ab67c0beep+216', '-0x1.3b399c7efd641p+180', '0x1.1cb46543dbb19p+159', '-0x1.05eacdf9f389dp+165', '0x1.9b694f5bb504dp+59', '-0x1.fe5067eead886p+193', '0x1.1044355abfc8cp+181', '-0x1.28d86029a3865p+139', '-0x1.47a26519eaae1p+150', '0x1.fd8a54b4ef522p+64', '-0x1.0a93c86ea96f0p+113', '0x1.3d678b43c1e02p+73', '0x1.57905143f8ac8p+204', '-0x1.859904d5393b9p+211', '0x1.2cf8576f9fb6cp+197', '0x1.c821789e8b326p+140', '-0x1.d585436a42dacp+168', '-0x1.1d14293ac5a5cp+198', '0', '-0x1.935c8b9de8147p+57', '0x1.e8cc391651b65p+17', '0x1.34b672e705651p+180', '0x1.e1421af425ed1p+146', '0x1.547103d981e77p+124', '-0x1.f9b0049d4e3d9p+170', '0x1.0745841a5e0bcp+143', '0x1.e86ffb57d0661p+148', '-0x1.f8ee066f4116bp+147', '-0x1.99002522eaa54p+188', '0x1.1932e62946472p+176', '-0x1.40fd5a0f0e3d8p+11', '-0x1.743f7be5f68b2p+185', '0x1.fcc6f1ced0612p+115', '-0x1.87f99b0386559p+164', '0x1.26b78087a2b64p+75', '0x1.61d70e587c45cp+179', '-0x1.4d1f52ba5a0fdp+160', '0x1.e44f1cf4e4b24p+64']

SECTIONS['M2_sumprod'] = r"""
import math, fractions
assert math.sumprod([1e20, 1.0, -1e20], [1, 1, 1]) == 1.0
assert math.sumprod([0.1] * 10, [1.0] * 10) == 1.0
assert math.sumprod([1, 2, 3], [4, 5, 6]) == 32 and type(math.sumprod([1], [2])) is int
assert math.sumprod([2 ** 62, 2 ** 62], [4, 4]) == 2 ** 65
assert math.sumprod([1.5, True], [2, 3.0]) == 6.0 and math.sumprod([], []) == 0
F = fractions.Fraction
assert math.sumprod([F(1, 3), 0.5, 2], [3, 2, 0.25]) == 2.5
assert math.sumprod([F(1, 2)], [F(1, 3)]) == F(1, 6) and math.prod([F(1, 3), 3]) == 1
try:
    math.sumprod([1, 2], [3])
except ValueError:
    pass
else:
    raise AssertionError('unequal lengths')
""" + 'def sumprod_cases(n):\n    # Deterministic inputs from a 64-bit LCG (no dependence on `random`).\n    state = 0x2545F4914F6CDD1D\n    def nxt():\n        nonlocal state\n        state = (state * 6364136223846793005 + 1442695040888963407) % (1 << 64)\n        return state >> 11\n    def num():\n        r = nxt() % 10\n        if r < 7:\n            m = nxt() % (1 << 53)\n            e = int(nxt() % 121) - 60\n            v = m * 2.0 ** e\n            return -v if nxt() % 2 else v\n        if r < 9:\n            return int(nxt() % 2001) - 1000\n        return bool(nxt() % 2)\n    cases = []\n    for _ in range(n):\n        k = int(nxt() % 20) + 1\n        p = [num() for _ in range(k)]\n        q = [num() for _ in range(k)]\n        cases.append((p, q))\n    return cases\ndef show(v):\n    return v.hex() if isinstance(v, float) else repr(v)\n' + r"""
def show(v):
    return v.hex() if isinstance(v, float) else repr(v)
got = [show(math.sumprod(p, q)) for p, q in sumprod_cases(300)]
bad = [i for i, (g, w) in enumerate(zip(got, SUMPROD_EXPECTED)) if g != w]
assert not bad and len(got) == len(SUMPROD_EXPECTED), (len(bad), bad[:5])
"""

import sys

failed = []
for name, src in SECTIONS.items():
    try:
        exec(compile(src, f'<review_round11 {name}>', 'exec'),
             {'__name__': f'r11_{name}', 'SUMPROD_EXPECTED': SUMPROD_EXPECTED})
    except BaseException as e:
        failed.append(name)
        print(f'FAIL {name}: {type(e).__name__}: {e}'[:600])
if failed:
    print('review_round11: FAILED', failed)
    sys.exit(1)
print('review_round11: ok')
