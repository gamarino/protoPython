# os.pipe() returns non-inheritable descriptors (PEP 446).  They were
# inheritable, so subprocess's error pipe leaked into the child and Popen()
# waited for the child to exit: a child that reads its stdin pipe never did
# (`Popen(["cat"], stdin=PIPE, stdout=PIPE)` hung, and so did
# `communicate(input)` in test_base64 and test_os).
import os
import subprocess
import sys

r, w = os.pipe()
assert not os.get_inheritable(r) and not os.get_inheritable(w)
# os.write takes any bytes-like object, memoryview slices included.
m = memoryview(b"abcdef")
assert m[1] == ord("b") and m[1:3].tobytes() == b"bc" and bytes(m[-2:]) == b"ef"
assert os.write(w, m[1:3]) == 2 and os.write(w, bytearray(b"x")) == 1
assert os.read(r, 10) == b"bcx"
os.close(r)
os.close(w)

child = [sys.executable, "-c",
         "import sys; data = sys.stdin.buffer.read(); sys.stdout.buffer.write(data[::-1])"]
p = subprocess.Popen(child, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
out, err = p.communicate(b"abc\xff", timeout=60)
assert out == b"\xffcba", out
assert p.returncode == 0

# sys.stdout.buffer / sys.stderr.buffer write bytes, in order with text.
child = [sys.executable, "-c",
         "import sys; print('a', flush=True); sys.stdout.buffer.write(b'b\\n'); print('c')"]
out = subprocess.run(child, stdout=subprocess.PIPE, timeout=60).stdout
assert out.replace(b"\r\n", b"\n") == b"a\nb\nc\n", out
print("os_pipe_subprocess_stdin: ok")
