import os
print("urandom" in dir(os))
print(__import__("posix").urandom)
print(os.urandom)
