d = {"__name__": None, "__module__": "foo", "__slots__": ()}
print("keys:", d.keys())
print("starting loop")
for k, v in d.items():
    print(k, v)
print("finished loop")
