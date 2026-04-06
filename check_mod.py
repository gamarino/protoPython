import sys
import _collections_abc
print(f"DEBUG: _collections_abc = {_collections_abc}")
print(f"DEBUG: hasattr(_collections_abc, '_check_methods') = {hasattr(_collections_abc, '_check_methods')}")
try:
    attr = getattr(_collections_abc, '_check_methods', 'MISSING')
    print(f"DEBUG: getattr(_collections_abc, '_check_methods') = {attr}")
except Exception as e:
    print(f"DEBUG: getattr raised {e}")

print("DEBUG: dir(_collections_abc) contents:")
names = dir(_collections_abc)
for name in names:
    print(f"  - {name}")

try:
    from _collections_abc import _check_methods
    print(f"SUCCESS: _check_methods = {_check_methods}")
except ImportError as e:
    print(f"FAILURE: {e}")
