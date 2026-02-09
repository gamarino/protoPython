# test_batch1_builtins.py

def test_reflection():
    print("Testing globals() and locals()...")
    g = globals()
    assert 'test_reflection' in g
    
    x = 10
    l = locals()
    assert l['x'] == 10
    
    def inner(a):
        b = 20
        il = locals()
        assert il['a'] == 5
        assert il['b'] == 20
        print("Inner locals verified:", il)
    
    inner(5)
    print("Reflection tests passed!")

def test_vars():
    print("Testing vars()...")
    l = locals()
    v = vars()
    assert v == l
    
    class MyObj:
        def __init__(self):
            self.a = 1
    
    o = MyObj()
    vo = vars(o)
    assert vo['a'] == 1
    print("vars() tests passed!")

def test_environ():
    print("Testing os.environ (via _os)...")
    import _os
    if hasattr(_os, 'putenv'):
        _os.putenv("PROTO_TEST_VAR", "HELLO")
        assert _os.getenv("PROTO_TEST_VAR") == "HELLO"
        print("os.putenv verified.")
    
    if hasattr(_os, 'unsetenv'):
        _os.unsetenv("PROTO_TEST_VAR")
        assert _os.getenv("PROTO_TEST_VAR") is None
        print("os.unsetenv verified.")

if __name__ == "__main__":
    test_reflection()
    test_vars()
    test_environ()
    print("All Batch 1 verification tests FINISHED.")
