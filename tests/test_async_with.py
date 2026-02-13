class AsyncContext:
    async def __aenter__(self):
        print("Entering async with")
        return "ContextData"

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        print("Exiting async with, exc:", exc_type)
        return False

async def main():
    print("Starting async with")
    async with AsyncContext() as val:
        print("In body, val:", val)
    print("Done async with")

print("Test start")
coro = main()
try:
    while True:
        coro.send(None)
except StopIteration:
    print("Final result: Success")
except Exception as e:
    print("Error:", e)
