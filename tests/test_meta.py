import sys

class Meta(type):
    def __new__(metacls, name, bases, classdict):
        print(f"Meta.__new__ called for {name}")
        print(f"Meta.__new__: classdict address = {hex(id(classdict))}")
        classdict["added_by_meta"] = 42
        print(f"Meta.__new__: added_by_meta in classdict: {'added_by_meta' in classdict}")
        print(f"Meta.__new__: getattr(classdict, 'added_by_meta') = {getattr(classdict, 'added_by_meta', 'MISSING')}")
        return type.__new__(metacls, name, bases, classdict)

print(f"Meta address: {hex(id(Meta))}")
val = getattr(Meta, '__new__', 'MISSING')
print(f"Meta.__new__ value: {val}")
print(f"Meta.__new__ type: {type(val)}")

class MyClass(metaclass=Meta):
    pass

print(f"MyClass address: {hex(id(MyClass))}")
print(f"MyClass.added_by_meta = {getattr(MyClass, 'added_by_meta', 'MISSING')}")
print(f"MyClass.__dict__ type: {type(MyClass.__dict__)}")
print(f"MyClass.__dict__ keys: {list(MyClass.__dict__.keys()) if hasattr(MyClass.__dict__, 'keys') else 'no keys()'}")
print(f"added_by_meta in MyClass.__dict__: {'added_by_meta' in MyClass.__dict__}")
