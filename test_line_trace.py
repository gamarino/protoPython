import sys
import collections
ring_buffer = collections.deque(maxlen=100)

def trace_calls(frame, event, arg):
    if event == 'line':
        code = frame.f_code
        if 'functools' in code.co_filename or '_collections_abc' in code.co_filename:
            ring_buffer.append(f"{code.co_filename}:{frame.f_lineno} in {code.co_name}")
    return trace_calls

sys.settrace(trace_calls)

print("Starting import functools")
try:
    import functools
except BaseException as e:
    print("Exception", e)
finally:
    sys.settrace(None)
    print("Finished trace:")
    for line in ring_buffer:
        print(line)

