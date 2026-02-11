# Installation and Packaging Guide

This document provides detailed instructions for building, installing, and packaging `protoPython` from source.

## Prerequisites

- **CMake**: Version 3.20 or higher.
- **C++ Compiler**: A compiler with support for C++20 (e.g., GCC 11+, Clang 13+, or MSVC 2022+).
- **protoCore**: The foundational library must be located in a sibling directory named `protoCore`.

```bash
git clone <protoCore_url> ../protoCore
```

## Building from Source

### Linux and macOS

1.  **Configure the build**:
    ```bash
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    ```

2.  **Build the project**:
    ```bash
    cmake --build build -j$(nproc)
    ```

### Windows

1.  **Configure the build** (using Visual Studio generator):
    ```powershell
    cmake -B build -S . -A x64
    ```

2.  **Build the project**:
    ```powershell
    cmake --build build --config Release
    ```

---

## Installation

Once the build is complete, you can install `protoPython` to your system (or a specific prefix).

### Default Installation
```bash
sudo cmake --install build
```

### Custom Installation Prefix
```bash
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/path/to/custom/dir
cmake --build build --target install
```

### Installed Components
- **Binaries**: `protopy` and `protopyc` are installed to `bin/`.
- **Library**: `libprotoPython` is installed to `lib/`.
- **Headers**: Public headers are installed to `include/protoPython/`.
- **Standard Library**: Core Python files are installed to `share/protoPython/lib/python3.14/`.

---

## Packaging (CPack)

`protoPython` uses CPack to generate platform-specific installers and packages.

### Linux
Generates `.deb`, `.rpm`, and `.tar.gz` packages.
```bash
cd build
cpack -G "DEB;RPM;TGZ"
```

### macOS
Generates a `.dmg` (DragNDrop) installer.
```bash
cd build
cpack -G DragNDrop
```

### Windows
Generates an `.exe` (NSIS) installer and a `.zip` archive.
```powershell
cd build
cpack -G "NSIS;ZIP"
```

---

## Verification

After installation, verify that the runtime and compiler are functional:

1.  **Check version**:
    ```bash
    protopy --version
    ```

2.  **Run a simple script**:
    ```bash
    protopy -c "print('protoPython is ready')"
    ```

3.  **Verify the compiler**:
    ```bash
    protopyc --help
    ```
