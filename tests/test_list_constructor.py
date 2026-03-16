l1 = [1, 2, 3]
l2 = list(l1)
print("L1:", l1)
print("L2 (from L1):", l2)
if l1 == l2:
    print("LIST FROM LIST OK")
else:
    print("LIST FROM LIST FAILED")

t = (4, 5, 6)
l3 = list(t)
print("T:", t)
print("L3 (from T):", l3)
if l3 == [4, 5, 6]:
    print("LIST FROM TUPLE OK")
else:
    print("LIST FROM TUPLE FAILED")
