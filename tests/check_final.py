import gc
print(f"gc module: {gc}")
print(f"gc.collect(): {gc.collect()}")
print(f"gc.isenabled(): {gc.isenabled()}")

x = list[int]
print(f"list[int]: {x}")
print(f"type(list[int]): {type(x)}")

import types
print(f"types.GenericAlias: {types.GenericAlias}")
