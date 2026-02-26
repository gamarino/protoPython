def f(): pass
print("getting annotate")
getattr(f, "__annotate__")
print("got annotate")
