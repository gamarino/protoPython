try:
    import test_my_contextvars
except Exception as e:
    print(f"Exception Type: {type(e)}")
    print(f"Exception args: {e.args}")
    print(f"Exception name: {getattr(e, 'name', None)}")
    print(f"Exception path: {getattr(e, 'path', None)}")
