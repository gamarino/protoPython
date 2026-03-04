try:
    import types
except Exception as e:
    print("CAUGHT:", e)
print("types exists:", hasattr(types, '__name__'))
