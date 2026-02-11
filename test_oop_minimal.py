class Animal:
    def __init__(self, name):
        self.name = name
    def speak(self):
        print("Animal speaks")

class Dog(Animal):
    def __init__(self, name, breed):
        Animal.__init__(self, name)
        self.breed = breed
    def speak(self):
        print(self.name, "says Woof!")

d = Dog("Buddy", "Golden Retriever")
print(d.name)
print(d.breed)
d.speak()
