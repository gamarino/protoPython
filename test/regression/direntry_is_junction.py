# os.DirEntry.is_junction() exists (always False outside Windows), so os.walk,
# shutil.rmtree and tempfile.TemporaryDirectory cleanup work.
import os
import shutil
import tempfile

with tempfile.TemporaryDirectory() as tmp:
    sub = os.path.join(tmp, "sub")
    os.mkdir(sub)
    os.close(os.open(os.path.join(tmp, "file"), os.O_CREAT | os.O_WRONLY, 0o600))
    os.close(os.open(os.path.join(sub, "inner"), os.O_CREAT | os.O_WRONLY, 0o600))

    with os.scandir(tmp) as entries:
        seen = {}
        for entry in entries:
            assert entry.is_junction() is False
            seen[entry.name] = (entry.is_dir(), entry.is_file())
    assert seen == {"sub": (True, False), "file": (False, True)}, seen

    walked = sorted((os.path.relpath(root, tmp), sorted(dirs), sorted(files))
                    for root, dirs, files in os.walk(tmp))
    assert walked == [(".", ["sub"], ["file"]), ("sub", [], ["inner"])], walked

    # shutil.rmtree of a directory tree created inside the temporary directory.
    tree = os.path.join(tmp, "tree")
    os.makedirs(os.path.join(tree, "a", "b"))
    os.close(os.open(os.path.join(tree, "a", "b", "leaf"), os.O_CREAT | os.O_WRONLY, 0o600))
    shutil.rmtree(tree)
    assert not os.path.exists(tree)

# TemporaryDirectory removed the directory and its contents.
assert not os.path.exists(tmp)

print("direntry_is_junction: ok")
