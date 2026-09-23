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

On 2026-09-16 the suite has 410 tests.

### Using an installed protoCore

protoPython prefers an **installed protoCore CMake package** and falls back to the
sibling source tree only when no package is found and no prefix was named:

```bash
# protoCore installed in a default prefix: nothing to pass.
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release

# protoCore installed elsewhere.
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DPROTO_CORE_PREFIX=$HOME/.local
# equivalently
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/.local
```

The discovery is `find_package(protoCore 2.0 CONFIG)`, so the prefix must hold
`lib/cmake/protoCore/protoCoreConfig.cmake` — protoCore emits it from its own install
rules. **A prefix holding only `libprotoCore` and `protoCore.h` is no longer
accepted**: without the package configuration there is no way to tell protoCore 1.x
from 2.x, and linking the wrong major version is silent.

The version floor is `2.0` and the ceiling is the next major version: protoPython uses
no protoCore API newer than 2.0.0, and protoCore's major version and its soname move
together. protoPython additionally asserts that the package's `SOVERSION` is `2`.

Pass `-DPROTOCORE_REQUIRE_PACKAGE=ON` to forbid the developer fallback. **Every
packaging build must set it**: the fallback performs no version check and warns that it
must not be used to produce a distributable package.

Switching a build directory between the two modes leaves a stale
`PROTOCORE_REQUIRE_PACKAGE`/`protoCore_DIR` cache; delete the build directory instead
of reconfiguring in place.

> The test suite is only buildable in developer mode. protoPython has no GoogleTest of
> its own: `gtest_main` comes from the `FetchContent` call in protoCore's `test/`
> directory, which is only processed when protoCore is added as a subdirectory. In
> package mode, build the packaged targets explicitly:
>
> ```bash
> cmake --build build_pkg --target protopy protopyc protoPython
> ```

## Running from the build tree

The executables are `build_release/src/runtime/protopy` and
`build_release/src/compiler/protopyc`. When protoCore is built in-tree, the build RPATH
includes `build_release/src/library` and `build_release/protoCore`, so the executables
find `libprotoPython` and `libprotoCore` without `LD_LIBRARY_PATH`:

```bash
./build_release/src/runtime/protopy example.py
```

From the build tree, protopy uses the repository's `lib/python3.14` standard library,
whose absolute path is compiled into the binary (see
[Standard library location](#standard-library-location)). If the loader cannot find a
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

The protoPython install rules do not install protoCore, also when protoCore is built
in-tree: protoCore defines install rules only when it is the top-level project. Install
a protoCore that matches the one protoPython was built against separately (the DEB and
RPM packages depend on the `protocore` package for this reason). With protoCore in the
same prefix, the RPATHs `$ORIGIN/../<libdir>` of the installed executables and `$ORIGIN`
of `libprotoPython` find it without `LD_LIBRARY_PATH`; otherwise add its library
directory to `LD_LIBRARY_PATH`. An older `libprotoCore` found first on the loader path
(for example in `/usr/local/lib`) makes protopy fail with an undefined symbol error.

Add the prefix's `bin` directory to `PATH`:

```bash
export PATH=$HOME/.local/bin:$PATH
```

## Packages (CPack)

```bash
cmake -S . -B build_pkg -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<protocore-prefix> -DPROTOCORE_REQUIRE_PACKAGE=ON
cmake --build build_pkg --target protopy protopyc protoPython
cd build_pkg && cpack -G DEB
```

The generators are chosen at configure time, and the DEB and RPM generators are enabled
only when `dpkg` and `rpmbuild` are found — `cpack` aborts the whole run when a
generator's tool is missing, which would take the TGZ down with it. Each configure
prints whether a generator was enabled or disabled and why.

Package names are pinned rather than left to each generator's default casing:
`protopython` for DEB, `protoPython` for RPM. Both declare a bounded dependency on
protoCore's own package (`protocore (>= 2.0.0), protocore (<< 3.0.0)` for DEB;
`protoCore >= 2.0.0, protoCore < 3.0.0` for RPM). protoCore is never bundled.

### Platform verification status

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | TGZ, DEB (needs `dpkg`), RPM (needs `rpmbuild`) | Built, installed to a scratch prefix and smoke-tested, including the installed `<libdir>/protoPython/python3.14` standard library and the extracted `.deb` payload |
| macOS | DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | NSIS, ZIP | Configured and reviewed, **never built** — no Windows host |

RPM packaging is configured and reviewed but **never executed**: `rpmbuild` is not
installed on the host this was verified on.

### Standard library location

protopy chooses its standard library directory in this order:

1. `--stdlib <dir>`, as given;
2. the installed location compiled into the binary, `<prefix>/<libdir>/protoPython/python3.14`.
   It is stored relative to the `bin` directory (`../<libdir>/protoPython/python3.14`)
   and resolved against the directory of the protopy executable, never against the
   working directory, so a relocated prefix keeps working;
3. the source tree's `lib/python3.14`, compiled in as an absolute path, which is what a
   binary run from the build tree uses.

If none of these exists, protopy prints `protopy: standard library not found; use
--stdlib <dir>` and continues without a standard library. protopy does not search
parent directories, so it never picks up another interpreter's `lib/python3.14` (such
as CPython's in `/usr/local/lib`).

To compile in a different installed location, configure with
`-DSTDLIB_INSTALL_PATH=<dir>` (absolute, or relative to the executable's directory).

## Next steps

- [User Guide](USER_GUIDE.md): the `protopy` command line.
- [Examples](../examples/): example scripts and `embedding_sample.cpp`, a C++ program
  that embeds protoPython (built as `build_release/embedding_sample`).
- [C++ API Reference](CPP_API_REFERENCE.md): embedding protoPython in a C++ program.
