# __main__.__file__ is absolute, as in CPython since 3.9, while sys.argv[0]
# stays the path given on the command line. CMakeLists.txt runs this script by
# a RELATIVE path from the source directory. CPython makes the path absolute
# without normalizing it on POSIX (cwd + "/" + path, `..` kept) and with
# GetFullPathNameW on Windows (normalized); `python -c` has no __file__ in
# __main__ (protopy_c_main_has_no_file).
import os
import sys
import __main__

given = sys.argv[0]
assert not os.path.isabs(given), "run this test by a relative path, not %r" % given
assert os.path.isabs(__file__), "__file__ is not absolute: %r" % __file__
assert __main__.__file__ == __file__, (__main__.__file__, __file__)
if sys.platform == "win32":
    expected = os.path.abspath(given)
    assert os.path.normcase(__file__) == os.path.normcase(expected), (__file__, expected)
else:
    expected = os.getcwd() + "/" + given
    assert __file__ == expected, (__file__, expected)
assert os.path.samefile(__file__, given)
print("main_file_absolute: ok")
