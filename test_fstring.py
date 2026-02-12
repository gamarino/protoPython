name = "World"
x = 10
y = 20
s = f"Hello, {name}! x + y = {x + y}"
print(s)

assert s == "Hello, World! x + y = 30"

# Escaped braces
s2 = f"{{brace}} {x}"
print(s2)
assert s2 == "{brace} 10"

# Nested expressions
s3 = f"{[1, 2, 3][1]}"
print(s3)
assert s3 == "2"

print("F-String tests passed!")
