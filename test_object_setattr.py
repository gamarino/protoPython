print("Hasattr object __setattr__:", hasattr(object, "__setattr__"))
try:
    print("object.__setattr__:", object.__setattr__)
except Exception as e:
    print("Error:", type(e), e)
