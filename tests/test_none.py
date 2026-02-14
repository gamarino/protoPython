import os
import _os
k = "PROTO_N"
v = _os.getenv(k)
print("v is:", v)
print("None is:", None)
print("v is None:", v is None)
print("v is not None:", v is not None)
print("PROTO_N in os.environ:", "PROTO_N" in os.environ)
if "PROTO_N" in os.environ:
    print("os.environ['PROTO_N'] is:", os.environ['PROTO_N'])
print("os.environ.get('PROTO_N', 100) is:", os.environ.get("PROTO_N", 100))
