try:
    print("Getting __annotate__ on len")
    getattr(len, "__annotate__")
except AttributeError:
    print("Caught!")
