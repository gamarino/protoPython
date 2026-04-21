import _collections
print(f"_collections: {_collections}")
print(f"deque: {_collections.deque}")
print(f"deque.__class__: {getattr(_collections.deque, '__class__', 'N/A')}")
print(f"deque.__call__: {getattr(_collections.deque, '__call__', 'N/A')}")
d = _collections.deque()
print(f"instance: {d}")
