class DummyIterable:
    def __init__(self, items):
        self.items = items
        self.idx = 0
    def __iter__(self):
        return self
    def __next__(self):
        if self.idx < len(self.items):
            val = self.items[self.idx]
            self.idx += 1
            return val
        raise StopIteration

print("Testing frozenset with DummyIterable")
s = frozenset(DummyIterable(['a', 'b', 'c']))
print(s)
print("frozenset done")
