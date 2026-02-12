import os

# Helper for list() as it might not be fully functional for all iterables yet
def to_list(it):
    l = []
    for x in it:
        l.append(x)
    return l

print("--- Testing repr() and ascii() ---")
s = "hello\nworld'\""
print("repr(s): %s" % repr(s))
print("ascii(s): %s" % ascii(s))

s2 = "h\xc3\xa9llo" # UTF-8 for "héllo"
print("repr(s2): %s" % repr(s2))
print("ascii(s2): %s" % ascii(s2))

print("--- Testing format() ---")
print("format(42): %s" % format(42))

print("--- Testing locals() and vars() ---")
def test_locals():
    x = 10
    y = 20
    locs = locals()
    print("x in locals(): %s" % ('x' in locs))
    print("y in locals(): %s" % ('y' in locs))
test_locals()

print("--- Testing eval() and exec() ---")
g = {'y': 100}
l = {}
print("Before exec: y in g = %s" % ('y' in g))
print("g.get('y') = %s" % g.get('y'))
try:
    exec("print('INSIDE exec: y =', y); x = y + 1", g, l)
    print("l['x'] after exec: %s" % l.get('x'))
    res = eval("x + 1", g, l)
    print("eval result: %s" % res)
except Exception as e:
    print("Caught exception: %s" % e)

print("--- Testing os.environ ---")
import os
os.environ['PROTO_TEST'] = '123'
print("os.environ['PROTO_TEST']: %s" % os.environ.get('PROTO_TEST'))
keys = to_list(os.environ.keys())
print("'PROTO_TEST' in os.environ.keys(): %s" % ('PROTO_TEST' in keys))
del os.environ['PROTO_TEST']
print("'PROTO_TEST' in os.environ after del: %s" % ('PROTO_TEST' in os.environ))
