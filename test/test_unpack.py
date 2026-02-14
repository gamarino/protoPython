x = [1, 2]
y = [*x, 3]
print(y)
z = {'a': 1}
w = {**z, 'b': 2}
print(w)
s = {1, 2}
s2 = {*s, 3}
print(sorted(list(s2)))
t = (1, 2)
t2 = (*t, 3)
print(t2)
