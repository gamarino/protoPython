import os, shutil, tempfile, sys
d = tempfile.mkdtemp()
t0 = tempfile.tempdir
def leaked_descriptor(path):
    f = open(path, "w"); f.write("x"); return f.fileno()
def descriptor_is_open(fd):
    try: os.fstat(fd)
    except OSError: return False
    return True
def write_implicitly(path, text):
    open(path, "w").write(text)
fds = [leaked_descriptor(os.path.join(d, "g%d.txt" % i)) for i in range(8)]
for attempt in range(40):
    junk = [(i, i + 1) for i in range(2000)]
    still_open = [fd for fd in fds if descriptor_is_open(fd)]
a = tempfile.tempdir == t0
for i in range(20):
    write_implicitly(os.path.join(d, "f%d.txt" % i), "data %d" % i)
for attempt in range(40):
    junk = [(i, i + 1) for i in range(2000)]
b = tempfile.tempdir == t0
print("p14", a, b)
