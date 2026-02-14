# examples/generators.py
# This example demonstrates the use of generators and sub-generators (yield from)
# in ProtoPython's GIL-free environment.

def count_up_to(n):
    """Simple generator that counts from 1 to n."""
    count = 1
    while count <= n:
        yield count
        count += 1

def power_generator(base, limit):
    """Generator that yields powers of a base up to a limit."""
    val = 1
    while val <= limit:
        yield val
        val *= base

def composite_generator():
    """Composite generator using 'yield from' to delegate to sub-generators."""
    print("--- Starting Count ---")
    yield from count_up_to(5)
    
    print("--- Starting Powers of 2 ---")
    yield from power_generator(2, 64)

def main():
    print("Executing Composite Generator:")
    for value in composite_generator():
        print(f"Yielded: {value}")

if __name__ == "__main__":
    main()
