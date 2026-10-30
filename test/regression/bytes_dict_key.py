# Equal bytes objects are one dict key and one set element.  A bytes object
# was keyed by its cell's protoCore hash, so b"a" and bytes([97]) were two
# keys: {b"a": 1}[bytes([97])] raised KeyError.
a = b"a"
built = bytes([97])
sliced = b"xa"[1:]
assert {a: 1}[built] == 1
assert {a: 1}.get(sliced) == 1
assert len({a: 1, built: 2, sliced: 3}) == 1
assert built in {a} and len({a, built, sliced}) == 1
# hash(b"a") == hash("a"), but they are different keys.
d = {b"a": 1, "a": 2}
assert len(d) == 2 and d[b"a"] == 1 and d["a"] == 2


class MyBytes(bytes):
    pass


assert {a: 1}[MyBytes(b"a")] == 1
assert {b"": 0}[bytes()] == 0
print("bytes_dict_key: ok")
