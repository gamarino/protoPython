class MyClass:
    pass

m = MyClass()
try:
    setattr(m, 'infile', '-')
    print(f"m.infile: {m.infile}")
except Exception as e:
    print(f"Error on m: {type(e)}: {e}")

import argparse
n = argparse.Namespace()
try:
    setattr(n, 'infile', '-')
    print(f"n.infile: {n.infile}")
except Exception as e:
    print(f"Error on n: {type(e)}: {e}")
