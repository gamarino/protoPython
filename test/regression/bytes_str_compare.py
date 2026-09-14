"""bytes compare only with bytes-like objects: b'a' == 'a' is False, b'a' < 'a' raises."""


def raises(fn):
    try:
        fn()
    except TypeError:
        return True
    return False


assert (b'a' == 'a') is False and (b'a' != 'a') is True and ('a' == b'a') is False
assert b'a'.__eq__('a') is NotImplemented and b'a'.__lt__('a') is NotImplemented
assert raises(lambda: b'a' < 'a') and raises(lambda: 'a' >= b'a') and raises(lambda: b'a' <= [1])
assert b'a' < b'b' and b'ab' > b'a' and b'a' == b'a' and b'a' <= b'a'
assert bytearray(b'a') < b'b' and b'a' < bytearray(b'b') and bytearray(b'x') == b'x'
assert sorted([b'b', b'a']) == [b'a', b'b']
assert 'a' not in [b'a'] and b'a' in [b'a']
print("bytes str compare OK")
