def f():
  print("A")
  yield 1
  print("B")
  yield 2
  print("C")

print("Start")
for x in f():
  print(f"Got: {x}")
print("End")
