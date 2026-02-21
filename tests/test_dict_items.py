# test_dict_items.py
d = {'a': 1, 'b': 2}
items = list(d.items())
print("items:", items)

gen = ((v, k) for k, v in d.items())
print("gen:", list(gen))
