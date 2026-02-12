import sys
import os

def test_frames():
    print("Testing sys._getframe()...")
    f0 = sys._getframe(0)
    print("Frame 0:", f0)
    print("Frame 0 f_code:", f0.f_code)
    print("Frame 0 f_globals:", "PRESENT" if f0.f_globals else "MISSING")
    
    def bar(f0_caller):
        f1 = sys._getframe(1)
        print("Frame 1 (caller):", f1)
        return f1 == f0_caller
    
    print("bar() result (f1 == f0):", bar(f0))

def test_globals_locals():
    print("\nTesting globals() and locals()...")
    g = globals()
    l = locals()
    print("globals() keys:", "PRESENT" if 'test_frames' in g else "MISSING")
    print("locals() == globals() at module level:", l is g)
    
    def func():
        x = 10
        l_func = locals()
        print("locals() in func has x:", 'x' in l_func)
        return l_func['x']
    
    print("func() result:", func())

def test_environ():
    print("\nTesting os.environ synchronization...")
    print("Initial PATH:", os.environ.get('PATH')[:20] + "...")
    os.environ['PROTO_TEST'] = 'Batch1'
    print("os.environ['PROTO_TEST'] set to:", os.environ['PROTO_TEST'])
    
    # Try to verify via getenv if possible
    try:
        import _os
        print("_os.getenv('PROTO_TEST'):", _os.getenv('PROTO_TEST'))
    except ImportError:
        print("_os not available")

if __name__ == "__main__":
    test_frames()
    test_globals_locals()
    test_environ()
    print("\nBatch 1 Verification Done.")
