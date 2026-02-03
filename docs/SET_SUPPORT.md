# Set Support

Basic `set` prototype backed by `proto::ProtoSet`.

## Methods

- **`add(value)`**: Add element to set (in-place via new ProtoSet).
- **`remove(value)`**: Remove element from set.
- **`__len__`**: Number of elements.
- **`__contains__`**: Membership test.
- **`__bool__`**: Non-empty is truthy.
- **`__iter__`**: Iterator over elements.

## Usage

Create via `set()` from builtins (returns empty set) or instantiate with `setPrototype` and `__data__` = `context->newSet()->asObject()`.
