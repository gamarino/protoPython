async def simple():
    return 42

async def main():
    print("In main")
    x = await simple()
    print("Got:", x)
    return x + 1

print("Starting")
coro = main()
print("Coro created:", coro)
try:
    coro.send(None)
except StopIteration as e:
    print("Result:", e.value)
except Exception as e:
#    import traceback
#    traceback.print_exc()
    print("Error:", e)
