d = {"__name__": None, "__module__": "abc", "__slots__": ()}
print("items:", list(d.items()))
for n, v in d.items():
    print("processing", n)
print("done")
