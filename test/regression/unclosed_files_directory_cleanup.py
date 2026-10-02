# Directories holding files that were written and never closed can be removed,
# and close()/`with` release a descriptor at once.
#
# protoPython closes a file nobody closes only when it is collected (no
# reference counts), so such files are still open here. On Linux and macOS
# that never prevented removing them; on Windows files are opened with
# FILE_SHARE_DELETE and removed with POSIX semantics (NTFS), so they and their
# directory go at once there too. (On FAT, exFAT and network shares Windows
# keeps such a file "delete pending" until it is closed; documented, not tested.)
import os
import shutil
import tempfile


def write_implicitly(path, text):
    open(path, "w").write(text)  # never closed explicitly


def descriptor_is_open(fd):
    try:
        os.fstat(fd)
    except OSError:
        return False
    return True


# shutil.rmtree of a tree of unclosed files.
d = tempfile.mkdtemp()
for i in range(20):
    write_implicitly(os.path.join(d, "f%d.txt" % i), "data %d" % i)
os.mkdir(os.path.join(d, "sub"))
write_implicitly(os.path.join(d, "sub", "deep.txt"), "deep")
shutil.rmtree(d)
assert not os.path.exists(d), "rmtree left " + d

# os.remove and os.rmdir one by one.
d = tempfile.mkdtemp()
write_implicitly(os.path.join(d, "one.txt"), "1")
os.remove(os.path.join(d, "one.txt"))
os.rmdir(d)
assert not os.path.exists(d)

# os.replace over a file that is still open.
d = tempfile.mkdtemp()
target = os.path.join(d, "target.txt")
write_implicitly(target, "old")
with open(os.path.join(d, "new.txt"), "w") as f:
    f.write("new")
os.replace(os.path.join(d, "new.txt"), target)
with open(target) as r:
    assert r.read() == "new"
shutil.rmtree(d)

# TemporaryDirectory cleans up after files opened and dropped in its scope.
with tempfile.TemporaryDirectory() as td:
    for i in range(10):
        write_implicitly(os.path.join(td, "%d.txt" % i), "v%d" % i)
    contents = [open(os.path.join(td, "%d.txt" % i)).read() for i in range(10)]
    assert contents == ["v%d" % i for i in range(10)], contents
    os.mkdir(os.path.join(td, "sub"))
    write_implicitly(os.path.join(td, "sub", "deep.txt"), "deep")
assert not os.path.exists(td), "TemporaryDirectory left " + td

# Explicit close and `with` release the descriptor at once.
p = tempfile.mktemp()
f = open(p, "w")
fd = f.fileno()
f.close()
assert not descriptor_is_open(fd)
with open(p, "w") as g:
    fd = g.fileno()
assert not descriptor_is_open(fd)
os.remove(p)

print("unclosed_files_directory_cleanup: ok")
