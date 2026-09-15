# Installing protoPython

This guide describes how to build protoPython from source, run it from the build tree
and install it.

## Platform support

protoPython is developed and tested on Linux. The build requires a POSIX system: the
runtime uses POSIX interfaces such as `unistd.h` and `dlopen`, and the CMake files add
GCC/Clang compiler options (`-fno-delete-null-pointer-checks`,
`-ftls-model=initial-exec`). Windows and MSVC are not supported. The sources and CMake
files contain macOS-specific branches (executable path lookup, `@loader_path` RPATH),
but macOS is not a tested platform.

## Prerequisites

- **protoCore** ([github.com/numaes/protoCore](https://github.com/numaes/protoCore)):
  either checked out next to protoPython as `../protoCore` (built together with
  protoPython) or already installed (see [Using an installed protoCore](#using-an-installed-protocore)).
- **CMake** 3.20 or newer.
- **A C++20 compiler**: GCC or Clang.
- **Git**.
- **Python 3** (optional): some tests are registered only when CMake finds a Python 3
  interpreter.

## Building from source

### 1. Clone protoPython and protoCore side by side

```bash
git clone https://github.com/numaes/protoCore.git
git clone https://github.com/gamarino/protoPython.git
cd protoPython
```

### 2. Configure and build

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
```

When `PROTO_CORE_PREFIX` is not set, CMake adds `../protoCore` as a subdirectory and
builds it into `build_release/protoCore` as part of this build; no separate protoCore
build is needed.

### 3. Run the tests

```bash
ctest --test-dir build_release --output-on-failure
```

On 2026-09-15 the suite has 318 tests.

### Using an installed protoCore

Set `PROTO_CORE_PREFIX` to the prefix where protoCore is installed. CMake then looks
for `libprotoCore` under `lib` or `lib64` and for `protoCore.h` under `include` in that
prefix, uses them, and does not build protoCore from source. Configuration fails if
either is missing.

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DPROTO_CORE_PREFIX=/usr/local
```

## Running from the build tree

The executables are `build_release/src/runtime/protopy` and
`build_release/src/compiler/protopyc`. When protoCore is built in-tree, the build RPATH
includes `build_release/src/library` and `build_release/protoCore`, so the executables
find `libprotoPython` and `libprotoCore` without `LD_LIBRARY_PATH`:

```bash
./build_release/src/runtime/protopy example.py
```

From the build tree, protopy finds the repository's `lib/python3.14` standard library
by searching the parent directories of the executable. If the loader cannot find a
library (for example with a protoCore installed in a non-standard prefix), add its
directory to `LD_LIBRARY_PATH`.

## Installing

The install prefix defaults to CMake's `CMAKE_INSTALL_PREFIX` (`/usr/local` on Linux).
Set it at configure time to install elsewhere:

```bash
# User-local install (no root privileges needed)
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build_release
cmake --install build_release
```

The install rules place, under the prefix (`<libdir>` is CMake's
`CMAKE_INSTALL_LIBDIR`, usually `lib` or `lib64`):

| Content | Location |
|---------|----------|
| `protopy`, `protopyc` | `bin/` |
| `libprotoPython` | `<libdir>/` |
| protoPython headers | `include/protoPython/` |
| Standard library | `<libdir>/protoPython/python3.14/` |

When protoCore is built in-tree, protoCore's own install rules also install its
library. Installed executables use the RPATH `$ORIGIN/../<libdir>` and
`libprotoPython` uses `$ORIGIN`, so `LD_LIBRARY_PATH` is not needed. Add the prefix's
`bin` directory to `PATH`:

```bash
export PATH=$HOME/.local/bin:$PATH
```

### Standard library location after installation

By default protopy is compiled with the relative standard library path
`../<libdir>/python3.14`, which does not match the installed location
`<libdir>/protoPython/python3.14`; an installed protopy then fails to import
standard library modules. Either configure the build with the installed path:

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$HOME/.local \
  -DSTDLIB_INSTALL_PATH=$HOME/.local/lib/protoPython/python3.14
```

(replace `lib` with your `<libdir>` if it differs), or pass the directory at run time
with `protopy --stdlib <prefix>/<libdir>/protoPython/python3.14`.

## Next steps

- [User Guide](USER_GUIDE.md): the `protopy` command line.
- [Examples](../examples/): example scripts.
- [C++ API Reference](CPP_API_REFERENCE.md): embedding protoPython in a C++ program.
