import sys

print("sys.platform:", sys.platform)
print("sys.version:", sys.version)

def test_argv():
    print("sys.argv:", sys.argv)
    assert len(sys.argv) > 0

def test_path():
    assert isinstance(sys.path, list)
    print("sys.path length:", len(sys.path))

test_argv()
test_path()
print("test_sys passed")
