import operator
_tuplegetter = lambda index, doc: property(operator.itemgetter(index), doc=doc)

class_namespace = {"p": _tuplegetter(0, 'doc')}
print("class_namespace created")
try:
    result = type("TestTuple", (tuple,), class_namespace)
    print("type created:", result)
except Exception as e:
    print("Exception in type:", type(e), e)
