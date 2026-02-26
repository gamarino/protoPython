def test_func():
    try:
        print("eval inside function is:", eval)
        e = eval("1 + 1")
        print("eval result:", e)
    except Exception as exc:
        print("Exception:", type(exc), exc)

test_func()
