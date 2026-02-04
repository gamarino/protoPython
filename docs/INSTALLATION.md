# protoPython Installation Guide

This guide covers installation procedures for Linux (Debian/Ubuntu, Fedora/RHEL), Windows, and macOS.

## Prerequisites

- **CMake** 3.20 or later
- **C++20**-capable compiler (GCC 10+, Clang 12+, MSVC 2019+)
- **protoCore** — must be available in the parent directory of protoPython (`../protoCore`)

### ProtoCore compatibility

protoPython expects protoCore as a CMake subproject in a sibling directory (`../protoCore`). The protoCore version or commit is **not** pinned; the build uses whatever is present at configure time. For reproducible builds and CI, use a known protoCore commit (e.g. a tag or a specific commit hash) and document it in your environment. Breaking changes in protoCore (API or ABI) can require updates in protoPython; run the protoPython test suite (e.g. `ctest -R test_foundation_filtered test_execution_engine test_regr`) after updating protoCore.

---

## Linux

### Build from Source

```bash
# Clone or ensure protoCore and protoPython are siblings
# Directory layout: parent/
#   protoCore/
#   protoPython/

cd protoPython
mkdir -p build && cd build
cmake ..
cmake --build .
```

The `protopy` executable will be at `build/protopy`, and the shared library at `build/src/library/libprotoPython.so`.

### Install to System (make install)

```bash
cd build
cmake --build .
cmake --install . --component protoPython
```

Use `--component protoPython` to install only protoPython targets (recommended when protoCore is built as a subproject).

By default this installs:
- `protopy` → `/usr/local/bin/`
- `libprotoPython.so` → `/usr/local/lib/`
- `lib/python3.14/` → `/usr/local/share/protoPython/`

Override with:
```bash
cmake -DCMAKE_INSTALL_PREFIX=/opt/protoPython ..
```

### Debian / Ubuntu (.deb)

Generate a `.deb` package:

```bash
cd protoPython/build
cmake -DCPACK_GENERATOR=DEB \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DSTDLIB_INSTALL_PATH=/usr/share/protoPython/python3.14 ..
cmake --build .
cpack
```

`STDLIB_INSTALL_PATH` must match where the stdlib will be installed (`${CMAKE_INSTALL_PREFIX}/share/protoPython/python3.14`).

This produces `protopython-0.1.0-Linux.deb`. Install with:

```bash
sudo dpkg -i protopython-0.1.0-Linux.deb
```

If dependencies are missing:
```bash
sudo apt-get install -f
```

### Fedora / RHEL / CentOS (.rpm)

Generate an `.rpm` package:

```bash
cd protoPython/build
cmake -DCPACK_GENERATOR=RPM \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DSTDLIB_INSTALL_PATH=/usr/share/protoPython/python3.14 ..
cmake --build .
cpack
```

This produces `protopython-0.1.0-Linux.rpm`. Install with:

```bash
sudo rpm -ivh protopython-0.1.0-Linux.rpm
# or
sudo dnf install protopython-0.1.0-Linux.rpm
```

---

## Windows

### Build from Source

1. **Install build tools**
   - Visual Studio 2019 or later with "Desktop development with C++", or
   - Build Tools for Visual Studio, or
   - MinGW-w64 with GCC 10+

2. **Ensure protoCore is built** in a sibling directory (`../protoCore`).

3. **Configure and build**

   Using Visual Studio (Developer Command Prompt):

   ```cmd
   cd protoPython
   mkdir build && cd build
   cmake .. -G "Visual Studio 17 2022" -A x64
   cmake --build . --config Release
   ```

   Or with Ninja:

   ```cmd
   cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build .
   ```

   The executable will be at `build\Release\protopy.exe` (or `build\protopy.exe` with Ninja).

4. **Copy the standard library** — ensure `lib\python3.14\` is either next to `protopy.exe` or set `PROTOPY_STDLIB` to its path.

**Note:** The runtime uses POSIX APIs (e.g. `access`) in some paths. For full Windows support, MinGW or WSL may be preferred until native Windows ports are completed.

---

## macOS

### Build from Source

1. **Install Xcode Command Line Tools** (or full Xcode):
   ```bash
   xcode-select --install
   ```

2. **Optional: Homebrew** for CMake:
   ```bash
   brew install cmake
   ```

3. **Build**:
   ```bash
   cd protoPython
   mkdir -p build && cd build
   cmake ..
   cmake --build .
   ```

   The `protopy` executable will be at `build/protopy`.

### Install to System

```bash
cmake --install . --component protoPython
```

### Create a macOS Package (.pkg) — Optional

CPack can generate a `.pkg` installer:

```bash
cmake -DCPACK_GENERATOR=productbuild ..
cpack
```

---

## Environment Variables

| Variable | Description |
|----------|-------------|
| `PROTOPY_STDLIB` | Path to the standard library (`lib/python3.14/`). If unset, the build-time path is used. |
| `PROTOPY_BIN` | Used by tests; path to the `protopy` binary. |

---

## Verifying the Installation

```bash
protopy --help
protopy --dry-run --script some_script.py
```

Run tests:
```bash
cd build
ctest -R test_execution_engine
```
