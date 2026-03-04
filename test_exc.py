try:
    raise ImportError("my message")
except ImportError as e:
    print(e.args)
    print(str(e))
