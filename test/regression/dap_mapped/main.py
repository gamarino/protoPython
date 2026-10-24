import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import dap_helper_outside

SRC = "def gen_f(x):\n    y = x * 3\n    return y\n"
exec(compile(SRC, "<generated>", "exec"))
r = dap_helper_outside.helper(4)
print("helper", r)
print("gen", gen_f(5))
