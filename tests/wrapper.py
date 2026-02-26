import traceback
with open('test/cpython/test_generators.py') as f:
    code = f.read()
try:
    exec(code, {'__name__': '__main__', '__file__': 'test/cpython/test_generators.py'})
except Exception as e:
    print("--- TRACEBACK ---")
    traceback.print_exc()
    print("--- FINISHED ---")
