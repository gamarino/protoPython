class DummyCoro:
    def __await__(self): pass
    def send(self): pass
    def throw(self): pass
    def close(self): pass

method_names = ('__await__', 'send', 'throw', 'close')

for method in method_names:
    print("Checking method", method)
    for B in DummyCoro.__mro__:
        if method in B.__dict__:
            print("Found in", B)
            break
    else:
        print("Not found")

print("Done")
