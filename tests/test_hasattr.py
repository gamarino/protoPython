o = object()
o.foo = 42
print(f"hasattr(o, 'foo'): {hasattr(o, 'foo')}")
print(f"getattr(o, 'foo'): {getattr(o, 'foo')}")
print(f"hasattr(o, 'bar'): {hasattr(o, 'bar')}")
print(f"getattr(o, 'bar', 'MISSING'): {getattr(o, 'bar', 'MISSING')}")

o.baz = None
print(f"o.baz = None")
print(f"hasattr(o, 'baz'): {hasattr(o, 'baz')}")
print(f"getattr(o, 'baz'): {getattr(o, 'baz')}")
