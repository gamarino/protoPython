import _io
import builtins

print("_io:", _io)
x = _io._IOBase
print("_io._IOBase:", x)
print("_io._IOBase is None:", x is None)

y = _io.open
print("_io.open:", y)

z = _io.BlockingIOError
print("_io.BlockingIOError:", z)

print("dir(_io):", dir(_io))

try:
    print("MyIO test...")
    class MyIO(_io._RawIOBase):
        pass
    print("MyIO created:", MyIO)
except Exception as e:
    print("MyIO creation failed:", e)
