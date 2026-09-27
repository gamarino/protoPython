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

Last verified 2026-09-27 against protoPython 1.0.0 and protoCore 2.5.0
(`PROTOCORE_ABI_SOVERSION 3`), built with `-DPROTOCORE_REQUIRE_PACKAGE=ON` so the
sibling developer fallback was a hard error and the package was the only source
of protoCore.

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux / Debian-Ubuntu | TGZ, DEB | **VERIFIED.** Installed with `dpkg -i` as root in a throwaway `ubuntu:24.04` container and run there from `/usr/bin/protopy`, outside any repository, with no `LD_LIBRARY_PATH` and no `PROTO*` variable set. `import json` resolved out of the installed `<libdir>/protoPython/python3.14`. |
| Linux / Fedora-RHEL | TGZ, RPM | **UNVERIFIED — blocked, and the reason is specific.** `cpack -G RPM` runs, but Fedora's `brp-mangle-shebangs` fails the build on the bundled CPython standard library: `ERROR: ambiguous python shebang in .../encodings/rot_13.py: #!/usr/bin/env python`. That is fatal, so no RPM is produced. The build itself is fine; only the RPM packaging step is blocked. It needs a maintainer decision (suppress `__brp_mangle_shebangs`, or correct the shebang in the shipped stdlib). |
| macOS | DragNDrop | **UNVERIFIED.** Configured and reviewed only; there is no macOS host here. Review is not verification. |
| Windows | NSIS, ZIP | **UNVERIFIED.** Configured and reviewed only; there is no Windows host here. |

### Portability fixed while verifying

Two translation units called `std::reverse` and the `std::max(initializer_list)`
overload without including `<algorithm>`
(`src/library/Compiler.cpp`, `src/library/SelectModule.cpp`). GCC 13 reaches
`<algorithm>` transitively through another header and GCC 14 does not, so
protoPython did not compile at all on Fedora 41. Both includes are now explicit.

### Package size

`libprotoPython.so.1.0.0` ships at 47,126,928 bytes with DWARF debug information,
because `src/library/CMakeLists.txt` applies `-g` unconditionally by design.
Stripped, the same library is 3,327,024 bytes — 14.2x smaller — and it is what
makes the DEB 31.7 MB rather than roughly 3 MB. The trade is deliberate; the
price is recorded here so it can be reconsidered knowingly.

### Known defect: the DEB dependency floor does not encode the ABI

The `Depends` field is a *version range*, and on its own that range is not an ABI
check. `PROTOCORE_ABI_SOVERSION` went from `2` to `3` in protoCore **2.2.0**, so
protoCore 2.0.0 and 2.1.0 carry `libprotoCore.so.2` while 2.2.0 and later carry
`libprotoCore.so.3`. A floor of ``2.0.0`` therefore admits a protoCore whose
SONAME this package was not linked against.

This was demonstrated, not argued. A decoy `protocore` 2.1.0 package providing
only `libprotoCore.so.2` was installed in a container; `dpkg -i` then accepted
this package, and the installed binary failed to start with
`libprotoCore.so.3: cannot open shared object file`. The install succeeded and
the program did not run.

Two things limit the damage, and one closes it:

- At **build** time the failure is loud, not silent. `find_package(protoCore …)`
  alone does accept a SOVERSION-2 protoCore, but `CMakeLists.txt` follows it with
  an explicit `protoCore_SOVERSION` assertion against `PROTOCORE_ABI_SOVERSION`,
  which stops configuration with a `FATAL_ERROR` naming both numbers. Verified by
  configuring against a complete forged 2.1.0 / SOVERSION 2 prefix.
- The **RPM** does not have this hole. `rpm` generates
  `Requires: libprotoCore.so.3()(64bit)` automatically from the linked binary, and
  that requirement is on the SONAME rather than the version. Verified: the decoy
  protoCore 2.1.0 does not satisfy it and `rpm -i` refuses.
- Raising the DEB floor to `2.2.0`, the first protoCore that shipped SOVERSION 3,
  would make the DEB range agree with the ABI. That is a packaging change for the
  maintainer to take, and it is not made here.

### Known defect: the DEB does not refresh the shared-library cache

Neither this package nor protoCore's carries a `postinst` or an `ldconfig`
trigger, so `ldconfig -p` does not list `libprotoCore.so.3` after `dpkg -i`.
Programs still start, because each binary carries
`RUNPATH $ORIGIN/../${CMAKE_INSTALL_LIBDIR}` and because the library lands in a
directory the dynamic loader searches by default, but the cache is misleading.
Run `ldconfig` after installing. The RPM has no such defect.

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
