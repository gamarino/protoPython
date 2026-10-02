# os.symlink, os.lstat and shutil.copytree / rmtree over symbolic links.
#
# On Windows, os.stat_result has st_file_attributes and st_reparse_tag, and
# stat has the IO_REPARSE_TAG_* constants, as in CPython: shutil reads them to
# tell directory junctions from symbolic links (copytree, rmtree).
import os
import shutil
import stat
import sys
import tempfile

WINDOWS = os.name == "nt"

root = tempfile.mkdtemp()
src = os.path.join(root, "src")
os.mkdir(src)
os.mkdir(os.path.join(src, "sub"))
with open(os.path.join(src, "a.txt"), "w") as f:
    f.write("A")
with open(os.path.join(src, "sub", "b.txt"), "w") as f:
    f.write("B")

if WINDOWS:
    for name in ("st_file_attributes", "st_reparse_tag"):
        assert hasattr(os.stat_result, name), "os.stat_result has no " + name
    for name in ("IO_REPARSE_TAG_SYMLINK", "IO_REPARSE_TAG_MOUNT_POINT", "IO_REPARSE_TAG_APPEXECLINK"):
        assert isinstance(getattr(stat, name, None), int), "stat has no " + name
    fst = os.lstat(os.path.join(src, "a.txt"))
    assert fst.st_reparse_tag == 0, fst.st_reparse_tag
    assert not fst.st_file_attributes & stat.FILE_ATTRIBUTE_DIRECTORY
    dst_ = os.stat(src)
    assert dst_.st_file_attributes & stat.FILE_ATTRIBUTE_DIRECTORY

linked = True
try:
    os.symlink(os.path.join(src, "a.txt"), os.path.join(src, "link.txt"))
    os.symlink(os.path.join(src, "sub"), os.path.join(src, "linkdir"), target_is_directory=True)
except OSError as e:
    # Windows without the symbolic-link privilege or developer mode.
    if not WINDOWS:
        raise
    linked = False
    print("copytree_symlinks: SKIPPED the symbolic-link part: %s" % e)

if linked:
    link = os.path.join(src, "link.txt")
    assert os.path.islink(link)
    assert stat.S_ISLNK(os.lstat(link).st_mode)
    assert os.readlink(link) == os.path.join(src, "a.txt")
    assert os.path.isfile(link)
    if WINDOWS:
        lst = os.lstat(link)
        assert lst.st_reparse_tag == stat.IO_REPARSE_TAG_SYMLINK, lst.st_reparse_tag
        assert lst.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT
        # stat() follows the link: the target's attributes, no reparse tag.
        assert os.stat(link).st_reparse_tag == 0
    entries = {e.name: e for e in os.scandir(src)}
    assert entries["link.txt"].is_symlink()
    assert stat.S_ISLNK(entries["link.txt"].stat(follow_symlinks=False).st_mode)
    assert stat.S_ISREG(entries["link.txt"].stat().st_mode)

    # symlinks=True copies the links as links.
    dst = os.path.join(root, "dst")
    shutil.copytree(src, dst, symlinks=True)
    assert os.path.islink(os.path.join(dst, "link.txt"))
    assert os.readlink(os.path.join(dst, "link.txt")) == os.path.join(src, "a.txt")
    assert os.path.islink(os.path.join(dst, "linkdir"))

    # symlinks=False copies what they point to.
    dst2 = os.path.join(root, "dst2")
    shutil.copytree(src, dst2)
    assert not os.path.islink(os.path.join(dst2, "link.txt"))
    with open(os.path.join(dst2, "link.txt")) as f:
        assert f.read() == "A"
    assert os.path.isdir(os.path.join(dst2, "linkdir"))
    with open(os.path.join(dst2, "linkdir", "b.txt")) as f:
        assert f.read() == "B"

    # rmtree removes a tree with links in it, never what they point to.
    shutil.rmtree(dst)
    assert os.path.exists(os.path.join(src, "sub", "b.txt"))
else:
    shutil.copytree(src, os.path.join(root, "dst2"))

shutil.rmtree(root)
assert not os.path.exists(root)
print("copytree_symlinks: ok")
