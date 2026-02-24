import operator
print("operator.itemgetter:", operator.itemgetter)
from operator import itemgetter as _itemgetter
print("_itemgetter:", _itemgetter)
print("property:", property)
_tuplegetter = lambda index, doc: property(_itemgetter(index), doc=doc)
print("_tuplegetter(0, 'doc'):", _tuplegetter(0, 'doc'))
