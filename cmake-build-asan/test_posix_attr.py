import posix
print("urandom in dir:", "urandom" in dir(posix))
print(f"__keys__: {getattr(posix, '__keys__', 'MISSING')}")
print(f"__all__: {getattr(posix, '__all__', 'MISSING')}")
