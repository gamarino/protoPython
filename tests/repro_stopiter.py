async def my_coro():
    return "secret"

print("--- Checking coroutine return StopIteration ---")
c = my_coro()
try:
    c.send(None)
except StopIteration as e:
    print("Caught StopIteration from coro")
    print("e.value:", getattr(e, "value", "MISSING"))
except Exception as ex:
    print("Caught unexpected exception:", type(ex), ex)
