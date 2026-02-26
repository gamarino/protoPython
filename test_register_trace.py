import sys
import _py_abc

print("Loading collections.abc")
import _collections_abc

print("Finished importing collections_abc. Registering dummy.")
class DummyCoro:
    pass

class MyCoro(_collections_abc.Coroutine):
    def send(self): pass
    def throw(self): pass
    def close(self): pass
    def __await__(self): pass

ring_buffer = []

def trace_calls(frame, event, arg):
    if event == 'line':
        code = frame.f_code
        if '_py_abc' in code.co_filename or '_collections_abc' in code.co_filename:
            ring_buffer.append(f"{code.co_filename}:{frame.f_lineno} in {code.co_name}")
            if len(ring_buffer) > 50:
                ring_buffer.pop(0)
    return trace_calls

sys.settrace(trace_calls)
try:
    _collections_abc.Coroutine.register(DummyCoro)
except BaseException as e:
    pass
finally:
    sys.settrace(None)
    print("Finished trace:")
    for line in ring_buffer:
        print(line)
