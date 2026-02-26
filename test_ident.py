print("Testing unbound str.isidentifier...")
print("Type of str.isidentifier:", type(str.isidentifier))
res = str.isidentifier("abc")
print("Result of str.isidentifier('abc'):", res)

a = "abc"
print("Testing bound a.isidentifier...")
print("Type of a.isidentifier:", type(a.isidentifier))

print("Testing unbound str.split...")
print("Type of str.split:", type(str.split))

print("Testing bound a.split...")
print("Type of a.split:", type(a.split))
