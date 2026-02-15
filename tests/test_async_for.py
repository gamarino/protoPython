class AsyncIter:
    def __init__(self, count):
        self.count = count
        self.i = 0

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.i >= self.count:
            # print("Raising StopAsyncIteration")
            raise StopAsyncIteration
        val = self.i
        self.i += 1
        # print("Returning", val)
        return val

async def main():
    print("Starting async for")
    total = 0
    try:
        async for x in AsyncIter(5):
            print("Iter:", x)
            total += x
    except Exception as e:
        print("Caught error inside main-loop:", type(e), e)
        raise
    print("Done async for, total:", total)
    return total

print("Test start")
coro = main()
try:
    while True:
        # print("Sending None...")
        coro.send(None)
except StopIteration as e:
    print("Caught StopIteration at top level!")
    print("  type(e):", type(e))
    # print("  e.args:", e.args)
    print("  e.value:", getattr(e, "value", "MISSING_VALUE"))
    if e.value == 10:
        print("SUCCESS")
    else:
        print("FAILURE: expected 10, got", e.value)
except Exception as e:
    print("Caught unexpected error at top level:", type(e), e)
