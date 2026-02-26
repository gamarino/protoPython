print("type(type):", type(type))
print("id(type):", hex(id(type)))
from _py_abc import ABCMeta
print("ABCMeta:", ABCMeta, "id:", hex(id(ABCMeta)))
try:
    print("ABCMeta type:", type(ABCMeta))
    print("ABCMeta bases:", ABCMeta.__bases__)
except Exception as e:
    print("Error introspecting ABCMeta:", e)

from _collections_abc import Sized
print("Sized:", Sized, "id:", hex(id(Sized)))
print("type(Sized):", type(Sized))
try:
    print("Sized is type(Sized):", Sized is type(Sized))
    print("Sized.__class__ is Sized:", Sized.__class__ is Sized)
    print("Sized.__class__.__name__:", Sized.__class__.__name__)
except Exception as e:
    print("Error introspecting Sized further:", e)
