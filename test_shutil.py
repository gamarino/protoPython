try:
    import shutil
except Exception as e:
    print("Caught:", type(e))
    print("Message:", repr(e.args))
    print("Traceback available?", hasattr(e, "__traceback__"))
    if hasattr(e, "__traceback__") and e.__traceback__:
        tb = e.__traceback__
        while tb:
            print(f"File {tb.tb_frame.f_code.co_filename}, line {tb.tb_lineno}")
            tb = tb.tb_next
