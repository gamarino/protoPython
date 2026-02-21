# test_dict_gen.py
d = {'a': 1, 'b': 2, 'c': 3}
r = dict((v, k) for (k, v) in d.items())
print("len of d:", len(d))
print("len of r:", len(r))
print("r is:", r)
