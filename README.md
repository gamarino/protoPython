# protoPython

A high-performance Python 3.14 compatible environment built on top of [protoCore](file:///home/gamarino/Documentos/proyectos/protoCore).

## Project Overview

ProtoPython aims to provide a GIL-less, highly parallel Python runtime and a C++ compiler for Python modules.

### Components

- **protoPython Library**: The core runtime library defining the Python environment over `ProtoSpace`.
- **protopy**: A GIL-less Python 3.14 compatible runtime.
- **protopyc**: A compiler that translates Python modules into C++ shared libraries based on `protoCore`.

## Author

Gustavo Marion <gamarino@gmail.com>

## License

MIT
