import contextvars
var = contextvars.ContextVar('var', default='Initial')
print("var:", var)
print("type(var):", type(var))
print("dir(var):", dir(var))
print("dir(type(var)):", dir(type(var)))
