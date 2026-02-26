import sys
try:
    print("DEBUG TEST SCRIPT: IMPORT UNITTEST START")
    import unittest
    print("unittest imported...")
except Exception as e:
    print("Error:", type(e).__name__, "object is not callable") 
    print("Args:", getattr(e, "args", ()))
    tb = getattr(e, "__traceback__", None)
    while tb is not None:
        frame = getattr(tb, "tb_frame", None)
        if frame:
            code = getattr(frame, "f_code", None)
            if code:
                fname = getattr(code, "co_filename", "?")
                cname = getattr(code, "co_name", "?")
                print(f"  File {fname}, line {tb.tb_lineno}, in {cname}")
        tb = getattr(tb, "tb_next", None)
