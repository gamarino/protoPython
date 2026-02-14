# Installing protoPython

This guide describes how to build and install `protoPython` from source on Linux, macOS, and Windows.

## Prerequisites

- **protoCore** — Required. You can either build protoCore as a sibling directory (development) or use a previously installed protoCore (packaging). See [protoCore](https://github.com/numaes/protoCore).
- **CMake** (3.20 or higher)
- **C++ Compiler** with C++20 support (GCC 11+, Clang 13+, or MSVC 2022+)
- **Git**

## Building from Source

### 1. Clone the repository and protoCore (for development)

For development, protoCore is often placed next to protoPython so CMake can build it via `add_subdirectory(../protoCore)`:

```bash
git clone https://github.com/numaes/protoPython.git
cd protoPython
git clone https://github.com/numaes/protoCore.git ../protoCore
# Optionally build protoCore first (protoPython will build it if missing):
# cmake -B ../protoCore/build -S ../protoCore
# cmake --build ../protoCore/build --target protoCore
```

**Using an installed protoCore (packaging / product shipping):**  
Set `PROTO_CORE_PREFIX` to the install prefix where protoCore is installed (e.g. `/usr/local` or `$HOME/.local`). CMake will use that library and headers and will not build protoCore from source.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DPROTO_CORE_PREFIX=/usr/local
```

When `PROTO_CORE_PREFIX` is set, only protoPython is built; protoCore must be installed separately. The protoCore installation must include all headers that protoPython expects (e.g. `protoCore.h` and, if used by your protoCore build, `proto_internal.h`).

### 2. Configure the build

Use CMake to generate the build files. It is recommended to use a separate build directory.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
```

#### Installation prefix (where files go)

By default, `make install` (or `cmake --build build --target install`) installs under **`/usr/local`**: executables in `bin/`, libraries in `lib/`, headers in `include/`. That is the traditional Unix location for locally built software.

To install elsewhere, set `CMAKE_INSTALL_PREFIX` at configure time:

```bash
# System-wide (default; often requires sudo for install)
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr/local

# User-local (no sudo; FHS-style under your home directory)
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=$HOME/.local

# Install into the build tree (development; no system directories)
cmake -B build -S . -DCMAKE_INSTALL_PREFIX="$(pwd)/build/install"
```

See [Where to install: executables, headers, libraries](#where-to-install-executables-headers-libraries) below for standard layout and packaging.

### 3. Build and Install

```bash
cmake --build build
# Install (writes to CMAKE_INSTALL_PREFIX; optional for development)
cmake --build build --target install
```

Install places:
- **Executables** → `{prefix}/bin/` (e.g. `protopy`, `protopyc`)
- **Libraries** → `{prefix}/lib/` (e.g. `libprotoPython.so`, and protoCore if built with protoPython)
- **Headers** → `{prefix}/include/`
- **Standard library** → `{prefix}/lib/protoPython/python3.14/`

## Running from the build tree

If you do **not** run `install`, you can run the interpreter from the build directory. The binaries are under `build/src/runtime/protopy` and `build/src/compiler/protopyc`.

When protoCore is built in-tree (default), **RPATH is set** so these executables find `libprotoCore` and `libprotoPython` without setting `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`. Just run:

```bash
./build/src/runtime/protopy example.py
```

If you use a custom layout or an older build and the loader cannot find the libraries, set the library path manually:

**Linux:** `export LD_LIBRARY_PATH="$(pwd)/build/src/library:$(pwd)/build/protoCore:$LD_LIBRARY_PATH"`  
**macOS:** `export DYLD_LIBRARY_PATH="$(pwd)/build/protoCore:$(pwd)/build/src/library:$DYLD_LIBRARY_PATH"`

## Where to install: executables, headers, libraries

Standard practice follows the **Filesystem Hierarchy Standard (FHS)** and CMake’s `GNUInstallDirs`:

| Content     | Typical path under prefix   | Purpose |
|------------|-----------------------------|--------|
| Executables| `bin/`                      | User-facing programs (`protopy`, `protopyc`) |
| Libraries  | `lib/` (or `lib64/` on some distros) | Shared/static libs (`libprotoPython`, `libprotoCore`) |
| Headers    | `include/`                  | For compiling C++ code that uses protoPython/protoCore |

- **Default prefix `/usr/local`**: Common for “install from source” on a single machine. System packagers (Debian, Homebrew, etc.) usually use their own prefix (e.g. `/usr`, or a Cellar path) and handle `bin`, `lib`, and `include` under that.
- **User install (no root)**: Use a prefix inside your home directory, e.g. `$HOME/.local` (Linux) or `$HOME/local`. Then add `$HOME/.local/bin` to `PATH` and, if needed, `$HOME/.local/lib` to `LD_LIBRARY_PATH` (Linux) or `DYLD_LIBRARY_PATH` (macOS).
- **Packaging**: For distro packages or “real product shipping”, install protoCore as its own package, then configure protoPython with `-DPROTO_CORE_PREFIX=<prefix>`. Install only protoPython’s executables, library, headers, and stdlib under the chosen prefix.

So: **the right way** is prefix-based and FHS-style under that prefix; the exact prefix is up to you (system `/usr/local`, user `$HOME/.local`, or a custom path). Avoid installing into system directories by default if your goal is a non-root or development install; use `-DCMAKE_INSTALL_PREFIX` accordingly.

## Post-Installation

The `protopy` executable finds its standard library relative to its own location. **Installed binaries and libraries use RPATH**, so you do not need to set `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH` for normal use. After install, ensure the prefix `bin` directory is in your `PATH`:

```bash
# If you used the default /usr/local
export PATH=/usr/local/bin:$PATH

# If you used $HOME/.local
export PATH=$HOME/.local/bin:$PATH
```

### Windows

The installation layout on Windows follows standard conventions, with the `protoPython` DLL and `protopy.exe` in the same directory.

## Next Steps

Once installed, we recommend exploring:
- [**User Guide**](USER_GUIDE.md) — Learn how to use the `protopy` CLI.
- [**Examples**](../examples/) — Explore illustrative scripts and embedding samples.
- [**C++ API Reference**](CPP_API_REFERENCE.md) — Integrate ProtoPython into your host application.
