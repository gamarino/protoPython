import argparse
print("Imported argparse")
try:
    p = argparse.ArgumentParser(color=True)
    print("ArgumentParser created")
except Exception as e:
    print("Caught exception:", repr(e))
    tb = e.__traceback__
    while tb is not None:
        frame = tb.tb_frame
        code = frame.f_code
        print(f"  File \"{code.co_filename}\", line {tb.tb_lineno}, in {code.co_name}")
        tb = tb.tb_next
