# io.open_code(path) opens for reading in binary, as open(path, "rb") does:
# site.addpackage reads each .pth file through it and decodes the bytes, so a
# text-mode open_code broke `import site` wherever a .pth file was installed
# (for example an editable install in the user's site-packages).
import io
import os
import site
import sys
import tempfile

directory = tempfile.mkdtemp()
data_path = os.path.join(directory, "data.bin")
with open(data_path, "wb") as f:
    f.write(b"caf\xc3\xa9\n")

with io.open_code(data_path) as f:
    content = f.read()
assert isinstance(content, bytes), type(content)
assert content == b"caf\xc3\xa9\n", content

# A site directory with a .pth file that names a subdirectory.
extra = os.path.join(directory, "extra")
os.mkdir(extra)
with open(os.path.join(directory, "sample.pth"), "w") as f:
    f.write("# a comment\nextra\n")
site.addsitedir(directory)
assert extra in sys.path, sys.path

print("io_open_code: ok")
