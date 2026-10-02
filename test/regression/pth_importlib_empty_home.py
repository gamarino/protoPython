# A .pth line that imports importlib.util, processed by site.addsitedir, must
# leave the interpreter to exit cleanly: no atexit report and status 0.  CTest
# runs this with HOME set to the empty string (see CMakeLists.txt) and fails
# on any "Exception ignored" or "atexit" text in the output.
import os
import site
import sys
import tempfile

with tempfile.TemporaryDirectory() as sitedir:
    with open(os.path.join(sitedir, "protopy_probe.pth"), "w") as f:
        f.write("import importlib.util\n")
    site.addsitedir(sitedir)
    assert "importlib.util" in sys.modules

print("pth_importlib_empty_home: ok")
