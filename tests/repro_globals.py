import builtins
print(f"None id: {hex(id(None))}")
print(f"789 id: {hex(id(789))}")
g = globals()
g.update({"xyz": 789})
print(f"xyz in g.__data__: {'xyz' in g.__data__ if hasattr(g, '__data__') else 'N/A'}")
print(f"xyz in g.__keys__: {'xyz' in g.__keys__ if hasattr(g, '__keys__') else 'N/A'}")
print(f"getattr(g, 'xyz'): {repr(getattr(g, 'xyz'))}")
