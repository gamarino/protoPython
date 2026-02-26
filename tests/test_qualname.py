def test_func():
    pass

try:
    print("test_func.__qualname__ =", test_func.__qualname__)
except Exception as e:
    print(type(e), e)

import keyword
print("keyword.iskeyword type:", type(keyword.iskeyword))
try:
    print("keyword.iskeyword.__qualname__ =", keyword.iskeyword.__qualname__)
except Exception as e:
    print(type(e), e)
