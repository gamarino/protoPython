# examples/async_demo.py
# This example demonstrates basic async/await functionality in ProtoPython.

async def fetch_data(id):
    print(f"Fetch starting for ID: {id}")
    # Simulate an async operation
    # In a real scenario, this might be an I/O operation
    return f"Data for {id}"

async def process_batch(ids):
    print(f"Processing batch: {ids}")
    results = []
    for id in ids:
        result = await fetch_data(id)
        results.append(result)
    return results

async def main():
    print("--- Async Execution Started ---")
    ids = [101, 102, 103]
    batch_results = await process_batch(ids)
    
    for r in batch_results:
        print(f"Result: {r}")
    print("--- Async Execution Completed ---")

if __name__ == "__main__":
    # In ProtoPython, the top-level execution of a coroutine
    # is supported natively by the execution engine.
    import sys
    
    # We can run the coroutine directly
    coro = main()
    # The runtime engine handles the coroutine lifecycle
    # when it's returned or explicitly awaited.
    # For now, we simulate the entry point.
    pass
