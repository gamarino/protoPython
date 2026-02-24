with open("lib/python3.14/_collections_abc.py", "r") as f:
    text = f.read()

text = text.replace("Iterator.register(bytes_iterator)", "print('DEBUG: Iterator.register(bytes_iterator)', flush=True)\nIterator.register(bytes_iterator)")
text = text.replace("Iterator.register(bytearray_iterator)", "print('DEBUG: Iterator.register(bytearray_iterator)', flush=True)\nIterator.register(bytearray_iterator)")
text = text.replace("Iterator.register(dict_keyiterator)", "print('DEBUG: Iterator.register(dict_keyiterator)', flush=True)\nIterator.register(dict_keyiterator)")
text = text.replace("Iterator.register(dict_valueiterator)", "print('DEBUG: Iterator.register(dict_valueiterator)', flush=True)\nIterator.register(dict_valueiterator)")
text = text.replace("Iterator.register(dict_itemiterator)", "print('DEBUG: Iterator.register(dict_itemiterator)', flush=True)\nIterator.register(dict_itemiterator)")
text = text.replace("Iterator.register(list_iterator)", "print('DEBUG: Iterator.register(list_iterator)', flush=True)\nIterator.register(list_iterator)")
text = text.replace("Iterator.register(list_reverseiterator)", "print('DEBUG: Iterator.register(list_reverseiterator)', flush=True)\nIterator.register(list_reverseiterator)")
text = text.replace("Iterator.register(range_iterator)", "print('DEBUG: Iterator.register(range_iterator)', flush=True)\nIterator.register(range_iterator)")
text = text.replace("Iterator.register(longrange_iterator)", "print('DEBUG: Iterator.register(longrange_iterator)', flush=True)\nIterator.register(longrange_iterator)")
text = text.replace("Iterator.register(set_iterator)", "print('DEBUG: Iterator.register(set_iterator)', flush=True)\nIterator.register(set_iterator)")
text = text.replace("Iterator.register(str_iterator)", "print('DEBUG: Iterator.register(str_iterator)', flush=True)\nIterator.register(str_iterator)")
text = text.replace("Iterator.register(tuple_iterator)", "print('DEBUG: Iterator.register(tuple_iterator)', flush=True)\nIterator.register(tuple_iterator)")
text = text.replace("Iterator.register(zip_iterator)", "print('DEBUG: Iterator.register(zip_iterator)', flush=True)\nIterator.register(zip_iterator)")

with open("lib/python3.14/_collections_abc.py", "w") as f:
    f.write(text)
