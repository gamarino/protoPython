class AsyncIter:
    def __init__(self, count):
        self.count = count
        self.i = 0

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.i >= self.count:
            raise StopAsyncIteration
        val = self.i
        self.i += 1
        return val

async def main():
    print("Starting async for")
    total = 0
    async for x in AsyncIter(5):
        print("Iter:", x)
        total += x
    print("Done async for, total:", total)
    return total

print("Test start")
coro = main()
try:
    while True:
        coro.send(None)
except StopIteration as e:
    print("Final result:", e.value)
except StopAsyncIteration:
    print("Error: StopAsyncIteration leaked")
except Exception as e:
    print("Error:", e)
