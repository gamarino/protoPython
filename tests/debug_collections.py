print("Importing _collections_abc")
import _collections_abc
print("Importing sys")
import sys
print("Importing itertools")
from itertools import chain, repeat, starmap
print("Importing keyword")
from keyword import iskeyword
print("Importing operator")
from operator import eq, itemgetter
print("Importing reprlib")
from reprlib import recursive_repr
print("Importing _weakref")
from _weakref import proxy
print("Trying to import _collections")
try:
    from _collections import deque
    print("deque imported from _collections")
except ImportError:
    print("_collections not available")

print("Defining a simple class to see if __module__ is set")
class X: pass
print("X.__module__:", X.__module__)

print("Success reaching the end of debug script")
