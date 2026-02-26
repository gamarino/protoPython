generator = type((lambda: (yield))())
print("mod level generator:", generator)
