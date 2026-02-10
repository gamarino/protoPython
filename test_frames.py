import sys

def test_frame():
    f = sys._getframe(0)
    print("Frame:", f)
    print("f_back:", f.f_back)
    print("f_code:", f.f_code)
    print("f_globals:", f.f_globals)
    print("f_locals is f:", f.f_locals == f)
    
    print("globals() matches f.f_globals:", globals() == f.f_globals)
    # locals() in test_frame should include 'f' and 'inner'
    locs = locals()
    print("locals() has 'f':", 'f' in locs)
    print("locals() has 'inner':", 'inner' in locs)

    print("\nStarting inner test...")
    def inner(outer_f):
        x = 42
        f_inner = sys._getframe(0)
        print("Inner Frame:", f_inner)
        locs_inner = locals()
        print("Inner locals() has 'x':", 'x' in locs_inner)
        print("Inner locals()['x'] == 42:", locs_inner['x'] == 42)
        print("Inner f_back matches outer frame:", f_inner.f_back == outer_f)
        
    inner(f)

print("Basic object:", object())
test_frame()
