import collections
try:
    collections.namedtuple('TokenInfo', 'type')
    print("SUCCESS")
except TypeError as e:
    print(f"FAILED: {e}")

typename = 'TokenInfo'
print(f"type(typename): {type(typename)}")
print(f"str type: {str}")
print(f"Match: {type(typename) is str}")
