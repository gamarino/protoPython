import sys
import collections

history = collections.deque(maxlen=50)

def trace_calls(frame, event, arg):
    if event == 'line':
        co = frame.f_code
        if "protopy/lib" in co.co_filename or "protopy/test" in co.co_filename:
            history.append(f"{co.co_filename}:{frame.f_lineno} in {co.co_name}")
    return trace_calls

sys.settrace(trace_calls)

try:
    import test.cpython.test_generators
except Exception as e:
    sys.settrace(None)
    for line in history:
        print(line)
    print(f"CRASH: {type(e).__name__}: {e}")
