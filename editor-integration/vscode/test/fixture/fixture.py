def twice(n):
    doubled = n * 2
    return doubled


def risky(n):
    raise ValueError("boom %d" % n)


print("e2e-before")
print(twice(21))
try:
    risky(7)
except ValueError as e:
    r = "caught " + str(e)
print(r)
print("e2e-after")
