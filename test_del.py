print("--- Testing del on globals ---")
g = 10
print("g before del:", g)
del g
try:
    print("g after del:", g)
except NameError:
    print("g is gone (NameError)")
except:
    print("g is gone (other error)")

print("\n--- Testing del on attributes ---")
class A:
    pass
a = A()
a.x = 42
print("a.x before del:", a.x)
del a.x
try:
    print("a.x after del:", a.x)
except AttributeError:
    print("a.x is gone (AttributeError)")
except:
    print("a.x is gone (other error)")

print("\n--- Testing del on subscripts ---")
d = {'a': 1, 'b': 2}
print("d before del:", d)
del d['a']
print("d after del 'a':", d)

print("\n--- Testing del on locals ---")
def test_del_local():
    lv = 100
    print("lv before del:", lv)
    del lv
    try:
        print("lv after del:", lv)
    except NameError:
        print("lv is gone (NameError)")
    except UnboundLocalError:
        print("lv is gone (UnboundLocalError)")
    except:
        print("lv is gone (other error)")

test_del_local()
