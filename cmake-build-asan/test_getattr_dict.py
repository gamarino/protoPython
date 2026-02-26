def g(): pass
try:
    print("Trying to getattr __dict__ from function")
    d = getattr(g, '__dict__')
    print("Got dict! Type:", type(d))
except Exception as e:
    print("Exception during getattr:", type(e))

print("Trying dict.update with {}")
try:
    d = {}
    d.update({})
    print("dict.update worked")
except Exception as e:
    print("Exception during update:", type(e))
