import sys

try:
    import test.cpython.test_grammar
except Exception as e:
    print("CAUGHT EXCEPTION:", type(e))
    print("MESSAGE:", str(e))
    tb = e.__traceback__
    while tb is not None:
        frame = tb.tb_frame
        if frame is not None:
            code = frame.f_code
            if code is not None:
                print("  File", code.co_filename, "line", tb.tb_lineno, "in", code.co_name)
            else:
                print("  Frame at line", tb.tb_lineno, "has no code")
        else:
            print("  Traceback element has no frame")
        tb = tb.tb_next
