def f1():
    print("f1")

def f2():
    def nested():
        pass
    print("f2")

if __name__ == "__main__":
    f1()
    f2()
