def test_args(a, b, *args):
    print("--- Inside test_args ---")
    print("a:", a)
    print("b:", b)
    print("args:", args)
    return len(args)

def test_kwargs(a, b, **kwargs):
    print("--- Inside test_kwargs ---")
    print("a:", a)
    print("b:", b)
    print("kwargs:", kwargs)
    return len(kwargs)

print("--- Running *args and **kwargs tests ---")

l = [2, 3, 4]
print("Calling test_args(1, *l) where l=[2, 3, 4]")
res = test_args(1, *l)
print("Result of test_args:", res)

d = {"c": 3, "d": 4}
print("Calling test_kwargs(1, 2, **d) where d={'c': 3, 'd': 4}")
res = test_kwargs(1, 2, **d)
print("Result of test_kwargs:", res)

print("--- Tests finished ---")
