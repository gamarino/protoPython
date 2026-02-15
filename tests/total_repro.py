print("--- Checking StopIteration.value directly ---")
e = StopIteration("secret")
print("e.args:", e.args)
try:
    print("e.value:", e.value)
except AttributeError as err:
    print("Caught expected AttributeError on e.value:", err)

async def my_coro():
    return "secret_return"

async def test():
    try:
        await my_coro()
    except StopIteration as e:
        print("Caught StopIteration from await:", e)
        print("  e.value:", e.value)

import asyncio
# StopIteration from await should have value
try:
    c = my_coro()
    c.send(None)
except StopIteration as e:
    print("Caught StopIteration from manual send:", e)
    print("  e.value:", e.value)

print("\nSuccess reaching the end of total_repro.py")
