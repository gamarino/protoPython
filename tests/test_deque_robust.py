from collections import deque

def test_deque_iteration():
    print("Testing standard iteration...")
    d = deque([1, 2, 3])
    assert list(d) == [1, 2, 3]
    print("Standard iteration passed.")

def test_deque_mutation_during_iteration():
    print("Testing mutation during iteration...")
    d = deque([1, 2, 3])
    it = iter(d)
    next(it)
    d.append(4)
    try:
        next(it)
        print("FAIL: Mutation not detected")
    except RuntimeError as e:
        print(f"Caught expected RuntimeError: {e}")
        assert str(e) == "deque mutated during iteration"

def test_deque_reversed():
    print("Testing reversed iteration...")
    d = deque([1, 2, 3])
    assert list(reversed(d)) == [3, 2, 1]
    print("Reversed iteration passed.")

def test_deque_reversed_mutation():
    print("Testing mutation during reversed iteration...")
    d = deque([1, 2, 3])
    it = reversed(d)
    next(it)
    d.popleft()
    try:
        next(it)
        print("FAIL: Mutation not detected in reversed")
    except RuntimeError as e:
        print(f"Caught expected RuntimeError in reversed: {e}")
        assert str(e) == "deque mutated during iteration"

if __name__ == "__main__":
    test_deque_iteration()
    test_deque_mutation_during_iteration()
    test_deque_reversed()
    test_deque_reversed_mutation()
    print("All deque tests passed!")
