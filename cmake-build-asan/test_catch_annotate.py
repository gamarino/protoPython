def f(): pass

print("Before try")
try:
    print("getting annotate")
    getattr(f, '__annotate__')
    print("got it")
except AttributeError:
    print("Caught AttributeError")
print("After try")
