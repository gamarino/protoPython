# Installing protoPython

This guide describes how to build and install `protoPython` from source on Linux, macOS, and Windows.

## Prerequisites

- **CMake** (3.20 or higher)
- **C++ Compiler** with C++20 support (GCC 11+, Clang 13+, or MSVC 2022+)
- **Git**

## Building from Source

### 1. Clone the repository

```bash
git clone --recursive https://github.com/proto-language/protoPython.git
cd protoPython
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

## Site-Packages

`protoPython` also looks for modules in the following user-specific directory on Linux:
`~/.local/lib/protoPython/python3.14/site-packages`
