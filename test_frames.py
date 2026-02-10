import sys

def test_frames():
    print("--- test_frames START ---")
    f = sys._getframe(0)
    print("Frame level 0:", f)
    
    g = globals()
    l = locals()
    print("globals() type:", type(g))
    print("locals() type:", type(l))
    
    def inner(x):
        y = x + 1
        inner_locals = locals()
        print("Inner locals:", inner_locals)
        print("Inner locals['x']:", inner_locals['x'])
        print("Inner locals['y']:", inner_locals['y'])
        
        outer_frame = sys._getframe(1)
        print("Outer frame from inner:", outer_frame)
        return inner_locals

    res = inner(42)
    print("--- test_frames END ---")

if __name__ == "__main__":
    test_frames()
