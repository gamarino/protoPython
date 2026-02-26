import sys

def trace_calls(frame, event, arg):
    if event == 'call':
        code = frame.f_code
        print(f"Call to {code.co_name} at {code.co_filename}:{code.co_firstlineno}")
    elif event == 'return':
        pass
    return trace_calls

sys.settrace(trace_calls)

print("Starting import functools")
import functools
print("Finished import functools")
