def kw_defaults_test(a, *, b=2, c=3):
    print(a, b, c)

kw_defaults_test(1, c=4)
kw_defaults_test(1)
print("SUCCESS")
