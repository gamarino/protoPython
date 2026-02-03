# Exception Support

Exception scaffolding for protoPython. The `exceptions` module provides base types for raiseable exceptions.

## Types

- **Exception**: Base exception type.
- **KeyError**: Subclass of Exception for missing key lookups.
- **ValueError**: Subclass of Exception for invalid values.

## Usage

```python
# Import the exceptions module
import exceptions

# Create exception instances (callable types)
e = exceptions.Exception("something went wrong")
k = exceptions.KeyError("missing_key")
v = exceptions.ValueError("invalid value")
```

## Implementation Notes

- Exception instances store `__args__` (tuple/list of constructor arguments).
- `__repr__` returns a string like `KeyError('x')`.
- `raise_exception` and integration with the execution engine are planned for Step 8.
