d = {'a': 1, 'b': 2}
print("d before del:", d)
del d['a']
print("d after del 'a':", d)

import os
print("os.environ['PATH']:", os.environ.get('PATH') is not None)
os.environ['PROTO_DEL_TEST'] = 'XYZ'
print("os.environ['PROTO_DEL_TEST'] before del:", os.environ['PROTO_DEL_TEST'])
del os.environ['PROTO_DEL_TEST']
try:
    print("os.environ['PROTO_DEL_TEST'] after del:", os.environ['PROTO_DEL_TEST'])
except KeyError:
    print("os.environ['PROTO_DEL_TEST'] successfully deleted")
