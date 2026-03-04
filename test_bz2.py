try:
    import bz2
except Exception as e:
    print("Caught:", type(e), e)
    print("Is ImportError?", isinstance(e, ImportError))
