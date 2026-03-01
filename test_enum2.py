import sys
def my_en(it):
    for x in it:
        print("ITEM:", repr(x))
print("custom enumeration:")
my_en(iter('abc'))
