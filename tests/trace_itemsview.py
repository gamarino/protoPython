import sys
try:
    import _collections_abc
except Exception as e:
    tb = e.__traceback__
    while tb:
        print(f"File {tb.tb_frame.f_code.co_filename}, line {tb.tb_lineno}, in {tb.tb_frame.f_code.co_name}")
        tb = tb.tb_next
    print(f"{type(e).__name__}: {str(e)}")
