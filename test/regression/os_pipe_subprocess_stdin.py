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

if sys.platform == "win32":
    # protopy has no subprocess support on Windows yet.
    print("os_pipe_subprocess_stdin: ok")
    raise SystemExit(0)

# SIGPIPE is ignored, as in CPython: writing to a pipe nobody reads raises
# BrokenPipeError instead of killing the interpreter.
r, w = os.pipe()
os.close(r)
try:
    os.write(w, b"x")
except BrokenPipeError:
    pass
else:
    raise AssertionError("write to a closed pipe succeeded")
os.close(w)

child = [sys.executable, "-c",
         "import sys; data = sys.stdin.buffer.read(); sys.stdout.buffer.write(data[::-1])"]
p = subprocess.Popen(child, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE)
out, err = p.communicate(b"abc\xff", timeout=60)
assert (out, p.returncode) == (b"\xffcba", 0), (out, p.returncode, err)

# sys.stdout.buffer / sys.stderr.buffer write bytes, in order with text.
child = [sys.executable, "-c",
         "import sys; print('a', flush=True); sys.stdout.buffer.write(b'b\\n'); print('c')"]
r = subprocess.run(child, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
assert r.stdout == b"a\nb\nc\n", (r.stdout, r.returncode, r.stderr)
print("os_pipe_subprocess_stdin: ok")
