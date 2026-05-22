class T:
    def go(self):
        global MyC
        class MyC:
            pass
        return MyC

t = T()
result = t.go()
print("MyC:", result)
print("__module__:", result.__module__)
print("__qualname__:", result.__qualname__)
import sys
print("module reachable:", hasattr(sys.modules.get(result.__module__, None), 'MyC'))
import pickle
try:
    pickle.dumps(result, 0)
    print("PASS")
except Exception as e:
    print(f"FAIL: {type(e).__name__}: {e}")
