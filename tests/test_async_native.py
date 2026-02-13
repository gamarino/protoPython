async def async_func():
    print("A")
    await None
    print("B")
    return 42

coro = async_func()
print("Starting coro")
try:
    print("Yielded:", coro.send(None))
    print("Resuming...")
    coro.send(None)
except StopIteration as e:
    print("Result:", e.value)
except Exception as e:
    print("Error:", e)
