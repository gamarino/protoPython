def a():
    b()

def b():
    c()

def c():
    d()

def d():
    raise ValueError("Something went wrong")

a()
