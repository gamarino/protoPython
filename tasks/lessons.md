# Lessons Learned (protoPython)

Patterns and rules derived from implementation and corrections. Update after applying fixes.

## Next 100 Steps (v43–v47)

- **Foundation tests and module attributes**: When testing that a Python stdlib module "has" a function or class (e.g. `pickle.loads`, `io.BytesIO`), resolve the module from the Python environment. The resolved module may be the native module (e.g. `_io`) or the Python wrapper (e.g. `io` from lib). If the test expects `getAttribute("BytesIO")` and the loader populates the module from executing the Python file, ensure the module under test is the one from lib (e.g. `io`), not a native alias. Prefer asserting `__file__` and module non-null for stability when attribute names differ across load paths.
- **Pickle minimal format**: A custom minimal serialization format (type tag + length-prefixed or newline-terminated payloads) is easier to implement and debug than emulating the full pickle protocol. Document the format and supported types (int, float, str, bytes, list, dict with str keys) in the module docstring.
- **Logging Handler/Formatter**: Use a dict for the log record in the minimal implementation so that `%(message)s`-style formatting works with `fmt % record` without requiring a full logging.LogRecord class.
- **Batch docs and commits**: For each batch (v43–v47), create NEXT_20_STEPS_Vxx.md first, implement, add foundation tests, update STUBS.md and tasks/todo.md and IMPLEMENTATION_PLAN.md, then commit with message `feat(stdlib): Next 20 Steps vxx (steps xxx-xxx)`.
- **v47 consolidation**: HPY_INTEGRATION_PLAN and PACKAGING_ROADMAP are planning docs only; no runtime code. venv stub remains no-op create with minimal_activate_script_path for future use.

## Next 20 Steps (v48–v49)

- **ProtoCore immutable model**: Base objects are immutable; `setAttribute` returns a new object. Mutable objects (`newObject(true)`) hold a current immutable version; `setAttribute` updates `mutableRoot` so the same handle reflects attribute changes. When testing builtins `setattr`/`getattr`, direct `obj->setAttribute`/`obj->getAttribute` on the same mutable object work; `py_setattr`/`py_getattr` when obj comes from positional parameters do not persist (root cause TBD).
- **Foundation test workarounds**: When a native method returns nullptr in some contexts (e.g. math.log10, operator.invert), use equivalent alternatives in tests: math.log(x, 10) instead of math.log10(x); direct setAttribute/getAttribute instead of py_setattr/py_getattr. Document the root cause and DISABLED_ the failing test until fixed.
- **strFromResult helper**: For str methods (upper, lower, capitalize) that return raw ProtoString or Python wrapper (**data**), use a helper that checks obj->isString() first, then **data**->isString(), to safely extract the ProtoString for toUTF8String.
- **OperatorInvert fix**: Invoke native methods via the same path as Python: use `invertM->call(context, nullptr, "__call__", invertM, args, nullptr)` instead of `asMethod()(context, ...)`. The C++ harness was bypassing **call**; the fix is to use the call path, not to keep the test disabled.

## Next 100 Steps (v53–v57)

- **Foundation suite**: CTest runs a filtered gate; full suite (`test_foundation` without filter) may include DISABLED_ tests. Document known-issues matrix in TESTING.md.
- **Regrtest persistence**: `run_and_validate_output.py` verifies `--output` writes valid JSON with `passed`, `failed`, `total` keys. Use `REGRTEST_RESULTS` for persistence path.
- **Batch commits**: Each v53–v57 block: create NEXT_20_STEPS_Vxx.md, implement steps, update todo/IMPLEMENTATION_PLAN/STUBS/TESTING, commit with `docs(v5x): Next 20 Steps v5x (9xx–9xx); ...`.

## Next 20 Steps (v58–v62) — Block 1100-1200 V2

- **Ropes as ProtoTuple**: Strings are exclusively ProtoTuple; concat = one tuple with 2 slots (left, right) and actual_size; leaf = tuple of chars. Inline strings (≤7 ASCII) in tagged pointer. O(1) concat; getAt O(depth) then O(1) at leaf. See protoCore ROPES_AS_PROTOTUPLE.md.
- **ProtoExternalBuffer and Shadow GC**: External segment via aligned_alloc; processReferences empty; finalize() frees segment when cell is collected. Stable-address contract: getRawPointer valid until object is collected (no compaction).
- **GetRawPointer API**: ProtoObject::getRawPointerIfExternalBuffer(context) returns segment pointer for ProtoExternalBuffer else nullptr. Use for zero-copy interop; document stable-address in protoCore GC doc.
- **Swarm tests**: ExternalBufferGC and GetRawPointerIfExternalBuffer pass. OneMillionConcats and LargeRopeIndexAccess disabled by design—root cause is GC/rope on very large graphs; fix requires protoCore changes; no hack (do not enable broken tests).
- **OperatorInvert**: C++ test must use same path as Python: invertM->call(context, nullptr, "**call**", invertM, args, nullptr). Direct asMethod() bypasses **call** and returns nullptr for native ProtoMethodCell.

## Attribute Lookup and Presence (v72+)

- **getAttribute vs hasAttribute**: In `protoCore`, `getAttribute` is designed to return `PROTO_NONE` (Python's `None`) when an attribute is not found. This is a core design decision. To distinguish between a missing attribute and an attribute explicitly set to `None`, use `hasAttribute(context, name)`.
- **Python hasattr/getattr semantics**: `hasattr(obj, name)` must use `obj->hasAttribute()` to correctly return `False` for missing attributes. `getattr(obj, name, default)` must check `hasAttribute()` before returning the result of `getAttribute()`, to avoid returning `None` when a default is provided or an `AttributeError` should be raised.
- **NamedTuple**: `collections.namedtuple` is implemented as a factory function that returns a new class. This class has a `_fields` attribute (a tuple of field names) and a `_field_defaults` attribute (a dict mapping field names to default values). Instances of the named tuple are regular tuple objects with additional attribute accessors.
- **NamedTuple Implementation**: The `namedtuple` factory function creates a new class that inherits from `tuple`. It sets the `_fields` and `_field_defaults` class attributes and adds `__new__`, `__repr__`, `__getattr__`, `__setattr__`, and `__delattr__` methods. The `__new__` method creates a new tuple instance, and the attribute access methods delegate to the underlying tuple's `__getitem__` method.
- **NamedTuple Tests**: The `test_namedtuple.py` test suite verifies the behavior of `namedtuple`, including creating instances, accessing fields by name and index, and verifying that instances are hashable and comparable. It also tests the `_make` and `_asdict` class methods.
- **NamedTuple Equality**: `namedtuple` instances support equality comparison with other tuples and with other `namedtuple` instances of the same type. The comparison is based on the values of the elements in the tuple.
- **NamedTuple Hashing**: `namedtuple` instances are hashable if all their elements are hashable. The hash value is computed based on the hash values of the elements in the tuple.
- **NamedTuple `_make` Method**: The `_make(iterable)` class method creates a new instance of the named tuple from an iterable. It is equivalent to `cls(*iterable)`.
- **NamedTuple `_asdict` Method**: The `_asdict()` method returns a new `dict` that maps the field names to the corresponding values in the named tuple instance.
- **NamedTuple `_replace` Method**: The `_replace(**kwargs)` method returns a new instance of the named tuple with specified fields replaced by new values.

## General rules

- **Attribute lookup and presence**: In `protoCore`, `getAttribute` is designed to return `PROTO_NONE` (Python's `None`) when an attribute is not found. This is a core design decision. To distinguish between a missing attribute and an attribute explicitly set to `None`, use `hasAttribute(context, name)`.
- **Python hasattr/getattr semantics**: `hasattr(obj, name)` must use `obj->hasAttribute()` to correctly return `False` for missing attributes. `getattr(obj, name, default)` must check `hasAttribute()` before returning the result of `getAttribute()`, to avoid returning `None` when a default is provided or an `AttributeError` should be raised.
- **NamedTuple**: `collections.namedtuple` is implemented as a factory function that returns a new class. This class has a `_fields` attribute (a tuple of field names) and a `_field_defaults` attribute (a dict mapping field names to default values). Instances of the named tuple are regular tuple objects with additional attribute accessors.
- **NamedTuple Implementation**: The `namedtuple` factory function creates a new class that inherits from `tuple`. It sets the `_fields` and `_field_defaults` class attributes and adds `__new__`, `__repr__`, `__getattr__`, `__setattr__`, and `__delattr__` methods. The `__new__` method creates a new tuple instance, and the attribute access methods delegate to the underlying tuple's `__getitem__` method.
- **NamedTuple Tests**: The `test_namedtuple.py` test suite verifies the behavior of `namedtuple`, including creating instances, accessing fields by name and index, and verifying that instances are hashable and comparable. It also tests the `_make` and `_asdict` class methods.
- **NamedTuple Equality**: `namedtuple` instances support equality comparison with other tuples and with other `namedtuple` instances of the same type. The comparison is based on the values of the elements in the tuple.
- **NamedTuple Hashing**: `namedtuple` instances are hashable if all their elements are hashable. The hash value is computed based on the hash values of the elements in the tuple.
- **NamedTuple `_make` Method**: The `_make(iterable)` class method creates a new instance of the named tuple from an iterable. It is equivalent to `cls(*iterable)`.
- **NamedTuple `_asdict` Method**: The `_asdict()` method returns a new `dict` that maps the field names to the corresponding values in the named tuple instance.
- **NamedTuple `_replace` Method**: The `_replace(**kwargs)` method returns a new instance of the named tuple with specified fields replaced by new values.
- **Test files**: all tests must be in the `tests` directory and must be named `test_*.py`. All logs, snippets, etc must be in the `logs` directory and must be named `test_*.log`.
- **Test timeout**: all tests must complete in less than 60 seconds. Use allways the timeout command to avoid stalling the test runner.
- **Full implementation**: allways use the full implementation of a function, not the stub. Objective is to have a full implementation of the Python standard library in protoPython.
- **Use only protoCore's public interface**: you can inspect the source code of protoCore to find and debug problems, but only public interface of protoCore is allowed in protoPython. Memory management and thread management should be done by protoCore, do not use any internal implementation details.
- **AttributeError vs None**: In `protoCore`, `getAttribute` returns `PROTO_NONE` (Python's `None`) when an attribute is not found. This is a deliberate design choice to distinguish between a missing attribute and an attribute explicitly set to `None`. To check for missing attributes, use `hasAttribute(context, name)`.
- **hasattr/getattr semantics**: `hasattr(obj, name)` must use `obj->hasAttribute()` to correctly return `False` for missing attributes. `getattr(obj, name, default)` must check `hasAttribute()` before returning the result of `getAttribute()`, to avoid returning `None` when a default is provided or an `AttributeError` should be raised.
- **NamedTuple Implementation**: `collections.namedtuple` is implemented as a factory function that returns a new class. This class has a `_fields` attribute (a tuple of field names) and a `_field_defaults` attribute (a dict mapping field names to default values). Instances of the named tuple are regular tuple objects with additional attribute accessors.
- **NamedTuple Tests**: The `test_namedtuple.py` test suite verifies the behavior of `namedtuple`, including creating instances, accessing fields by name and index, and verifying that instances are hashable and comparable. It also tests the `_make` and `_asdict` class methods.
- **NamedTuple Equality**: `namedtuple` instances support equality comparison with other tuples and with other `namedtuple` instances of the same type. The comparison is based on the values of the elements in the tuple.
- **NamedTuple Hashing**: `namedtuple` instances are hashable if all their elements are hashable. The hash value is computed based on the hash values of the elements in the tuple.
- **NamedTuple `_make` Method**: The `_make(iterable)` class method creates a new instance of the named tuple from an iterable. It is equivalent to `cls(*iterable)`.
- **NamedTuple `_asdict` Method**: The `_asdict()` method returns a new `dict` that maps the field names to the corresponding values in the named tuple instance.
- **NamedTuple `_replace` Method**: The `_replace(**kwargs)` method returns a new instance of the named tuple with specified fields replaced by new values.
- **Stack Item Ordering (OP_STORE_SUBSCR / OP_DUP_TOP_TWO)**: Overhauled `OP_STORE_SUBSCR` to conform rigidly with Python 3.14 ordering: `TOS` is the key, `TOS1` is the container, and `TOS2` is the value. Prior iterations incorrectly inverted `container` and `key` extraction, failing `test_execution_engine`. Similarly, `OP_DUP_TOP_TWO` must preserve the stacking order: duplicating `a, b` into `a, b, a, b` where `b` is unconditionally the Top-Of-Stack (TOS).

## Thread Local GC and Exception Context Preservation (v73+)

- **py_thread for Thread-Local GC Roots**: `s_threadPendingException` and `s_threadTraceFunction` should not be raw C++ pointers without being rooted in the GC. Using `moduleRootsMutex` for this causes deadlocks when acquiring during `moduleRoots` iteration while the GC marks objects.
- **The py_thread Pattern**: The fix is to attach a `py_thread` Python object to the thread environment, and store all thread-local state as attributes on this object. By registering `py_thread` in a lock-free or pre-allocated global GC registry (like `space_->typePrototype` or a global dictionary), the `protoCore` GC organically scans thread locals without introducing new mutexes. The C++ code uses `thread_local const proto::ProtoObject* s_currentPyThread` for fast access.
- **Exception Wiping in Internal Calls**: When a Python `except` block catches an exception, it relies on `OP_RAISE_VARARGS 0` and the thread's pending exception state to re-raise. However, if internal C++ code executed during the block (like `hasattr`, `getattr`, or string joining) throws and catches *internal* exceptions (like `AttributeError` or `StopIteration`), it overwrites and clears the thread's global pending exception resulting in `RuntimeError: reraise outside of except block`.
- **Active Exception Stack**: The correct architecture to prevent exception dropping is to maintain an explicit active exception stack (similar to CPython's `sys.exc_info()`) managed by the `ExecutionEngine` and `Compiler`.
  - The engine must call `env->pushActiveException(exc)` right before dispatching to an `except` handler, storing the exception in a lock-free list inside the `py_thread` dict (`_active_excs`).
  - The compiler must emit `OP_POP_EXCEPT` at the end of every `except` block, ensuring the engine predictably pops the stack and does not leak references across scopes.
  - `OP_RAISE_VARARGS 0` must prioritize resolving its re-raise target from `env->getActiveException()` before checking the transient value stack.

## Garbage Collection and C++ Intermediate Allocations

- **GC Sweeping Mid-Method**: When performing multiple `allocCell` or `new` operations within a single C++ method in `protoCore`, early allocations that haven't yet been anchored to a long-lived object graph are vulnerable to being swept if the Garbage Collector thread triggers *during* the method's execution.
- **The Double-Free / Type Confusion Symptom**: If an unrooted intermediate cell is collected and added to the free list while a C++ method is still assembling it, a subsequent allocation in the same method or a concurrent thread might reuse that exact memory address. The method will finish assembling the object and overwrite the reused memory, causing memory corruption and segmentation faults (e.g., `ParentLinkImplementation` holding a tagged pointer).
- **The Context-Scoped Fix**: The correct `protoCore` pattern is to defer submitting the young generation (`lastAllocatedCell`) to the GC until the `ProtoContext` destructor. The GC sweep loop should only *scan* `lastAllocatedCell` for references to older objects to keep them alive, but it must not steal the list or collect the young objects. `submitYoungGeneration` should be called strictly when the context tears down, ensuring all intermediate C++ allocations survive the duration of the method.

## Stack Underflow in Except Blocks (v74+)

- **The `pass` node expression value bug**: In `Compiler.cpp`, `statementLeavesValue` was incorrectly returning `true` for `PassNode` (and `BreakNode`/`ContinueNode`). This caused the compiler to erroneously emit an extra `OP_POP_TOP` instruction whenever a `pass` statement was the last statement in an `except` block.
- **The Infinite Loop Consequence**: When `OP_POP_TOP` is emitted spuriously inside an `except` block that is nested within a `for` loop, the `OP_POP_TOP` silently consumes the `for` loop's iterator object from the VM stack. When the `except` block finishes and jumps back to the `OP_FOR_ITER` instruction, the stack is empty. `OP_FOR_ITER` fails to retrieve the iterator and assumes the iteration is complete or catches an internal exception, creating a silent, infinite loop.
- **The Fix**: Explicitly check for `PassNode`, `BreakNode`, and `ContinueNode` in `statementLeavesValue` and return `false`, preventing the compiler from emitting the spurious `OP_POP_TOP`.

## Builtin Types Initialization and Prototypes

- **Environment Prototype Linkage**: When creating specialized prototypes in `BuiltinsModule.cpp` (like `rangeIterProto`), it is not enough to simply create `newObject` and attach it to the `builtins` module. You must explicitly register it with the `PythonEnvironment` using setters (e.g., `pEnv->setRangeIteratorProto()`), otherwise newly instantiated builtins (like `iter(range())`) will fall back to using `object` as their `__class__`, breaking subclass checks and ABCs like `_collections_abc` (e.g., resulting in `RuntimeError: Refusing to create an inheritance cycle`).
- **Descriptor Protocol (**get**)**: In ProtoPython, as in CPython, the descriptor protocol (`__get__`, `__set__`) is invoked by `getAttribute` looking up the dunder methods on the **TYPE** of the attribute value, not on the instance itself. If a builtin function (like `property`) dynamically returns a raw object decorated with `__get__` (but no `__class__`), `getAttribute` will evaluate its type as `object`, which has no `__get__`, causing the descriptor to be silently completely bypassed. Builtin descriptors must have a specialized prototype (e.g., `propertyProto`) where `__get__` is defined, and the instances returned must have their `__class__` set to this prototype.
- **Native Method Binding**: When looking up native methods (e.g., `__reversed__`, `__len__`) inside builtin functions, always use `env->getAttribute(context, obj, name)` instead of `obj->getAttribute(context, name)`. The former correctly triggers the descriptor protocol (calling `__get__` on the method type) to return a bound method (`ProtoMethodCell` with a valid `self`), while the latter returns the raw unbound method object, which leads to `TypeError` when explicitly invoked.

## GC and processReferences (Tag Stripping)

- **CRITICAL TAGGED POINTER 2**: In `protoCore`, many pointers held by objects (like `TupleDictionary` keys, list nodes, etc.) can potentially be purely embedded values such as small integers. These embedded values encode their type into the lower bits of the pointer handle (e.g., `0x1c081`).
- **Safe GC Notification**: Submitting a raw handle directly to the GC via the `method(...)` callback inside a class's `processReferences` override is strictly unsafe. The GC will attempt to dereference the tagged pointer, causing a hard segmentation fault.
- **Validation Protocol**: You must *never* pass an object directly to `method(...)`. You must first enforce validation using `ProtoObject::isCellPointer(ptr)` to filter out non-cells, and then aggressively strip the tag discriminators by wrapping the invocation argument with `ProtoObject::asCellPointer(ptr)`. (e.g., `if (node && ProtoObject::isCellPointer(reinterpret_cast<const ProtoObject*>(node))) method(ctx, self, ProtoObject::asCellPointer(reinterpret_cast<const ProtoObject*>(node)));`).
