def safe_div(a, b):
    try:
        return a / b
    except ZeroDivisionError as e:
        print("error:", e)
        return None
    finally:
        print("done")


def fail():
    raise ValueError("bad")
