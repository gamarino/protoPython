# Notes for AI coding agents

protoPython is a GIL-free Python 3.14 runtime that embeds
[protoCore](https://github.com/numaes/protoCore) as its object model and
garbage collector.

Before writing code that keeps a `ProtoObject*` across asynchronous, thread or
native-call boundaries, read [docs/GC_BRIDGING.md](docs/GC_BRIDGING.md).

Build and test (protoCore checked out next to this repository as `../protoCore`):

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
ctest --test-dir build_release --output-on-failure
```

Repository conventions, test layout and commit expectations are described in
[CONTRIBUTING.md](CONTRIBUTING.md).
