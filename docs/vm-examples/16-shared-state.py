counter = 0


def bump():
    global counter
    counter += 1


class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
