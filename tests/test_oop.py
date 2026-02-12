class Animal:
    def __init__(self, name):
        print("Animal init")
        self.name = name
    
    def speak(self):
        print("Animal speaks")

class Dog(Animal):
    def __init__(self, name, breed):
        print("Dog init")
        # For now, we might not have super() working perfectly, 
        # let's test direct parent call if super() fails
        Animal.__init__(self, name)
        self.breed = breed
    
    def speak(self):
        print(self.name, "says Woof!")

print("Creating dog...")
d = Dog("Buddy", "Golden Retriever")
print("Name:", d.name)
print("Breed:", d.breed)
d.speak()

print("Testing attribute deletion...")
del d.breed
try:
    print(d.breed)
except Exception as e:
    print("Caught expected error after deletion")

print("OOP test complete")
