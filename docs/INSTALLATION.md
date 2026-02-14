# Installing protoPython

This guide describes how to build and install `protoPython` from source on Linux, macOS, and Windows.

## Prerequisites

- **protoCore** — Required. The build expects protoCore to be available; the standard layout is to have protoCore as a sibling directory (e.g. `../protoCore`). Build protoCore first. See [protoCore](https://github.com/numaes/protoCore) or your local clone.
- **CMake** (3.20 or higher)
- **C++ Compiler** with C++20 support (GCC 11+, Clang 13+, or MSVC 2022+)
- **Git**

## Building from Source

### 1. Clone the repository and protoCore

Ensure protoCore is present next to protoPython so CMake can use `add_subdirectory(../protoCore)`:

```bash
git clone --recursive https://github.com/proto-language/protoPython.git
cd protoPython
# If protoCore is not a submodule, clone it beside protoPython:
# git clone https://github.com/numaes/protoCore.git ../protoCore
# Build protoCore first:
# cmake -B ../protoCore/build -S ../protoCore
# cmake --build ../protoCore/build --target protoCore
```

### 2. Configure the build

Use CMake to generate the build files. It is recommended to use a separate build directory.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
```

#### Custom Installation Prefix

You can specify where `protoPython` should be installed using `CMAKE_INSTALL_PREFIX`:

```bash
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr/local
```

### 3. Build and Install

```bash
cmake --build build --target install
```

This will install:
- `protopy` executable to `bin/`
- `protoPython` shared library to `lib/` (or `bin/` on Windows)
- Python Standard Library to `lib/protoPython/python3.14/`

## Post-Installation

The `protopy` executable automatically resolves the location of its standard library relative to its own path. 

### Linux/macOS

Ensure the `bin` directory is in your `PATH`:

```bash
export PATH=/usr/local/bin:$PATH
```

### Windows

The installation layout on Windows follows standard conventions, ensuring that the `protoPython` DLL and `protopy.exe` are in the same directory.

## Next Steps

Once installed, we recommend exploring:
- [**User Guide**](USER_GUIDE.md) — Learn how to use the `protopy` CLI.
- [**Examples**](../examples/) — Explore illustrative scripts and embedding samples.
- [**C++ API Reference**](CPP_API_REFERENCE.md) — Integrate ProtoPython into your host application.
