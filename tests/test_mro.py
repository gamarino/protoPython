x = type(iter([]))
print("list_iterator type:", x)
print("list_iterator mro:", x.__mro__)
print("Done mro")

for B in x.__mro__:
    print("Checking", B)
    if '__iter__' in B.__dict__:
        print("has __iter__")
    
print("Done list_iterator check")
