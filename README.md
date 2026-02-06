# protoPython

A high-performance Python 3.14 compatible environment built on top of [protoCore](../protoCore/).

## Community & Open Review

Beyond formal audits, this project is officially **open for Community Review and Suggestions**.

We welcome architectural feedback, edge-case identification, and performance critiques. While the core vision is firm, the path to perfection is a collective effort of the "Swarm."

## Project Overview

ProtoPython aims to provide a GIL-less, highly parallel Python runtime and a C++ compiler for Python modules.

### Why protoPython?

- **Immutable Integrity**: Leverages protoCore's immutable memory model for thread-safety by design.
- **Zero-Copy Interop**: The UMD allows passing massive data buffers to C-libraries (like NumPy) without moving a single pointer.
- **Hardware-Aware**: Optimized for modern CPU cache lines through 64-byte cell alignment.

### Components

- **protoPython Library**: The core runtime library defining the Python environment over `ProtoSpace`.
- **protopy**: A GIL-less Python 3.14 compatible runtime.
- **protopyc**: A compiler that translates Python modules into C++ shared libraries based on `protoCore`.

## The Swarm of One

**The Swarm of One** enables a paradigm shift in Python development. In protoPython, we moved from core infrastructure to a fully functional runtime—including a **Unified Memory Bridge (UMD)**—in record time. By orchestrating a swarm of specialized AI agents, a single architect has built a GIL-less environment where Python objects live in the same 64-byte cell universe as protoCore and protoJS. This is the democratization of high-level engineering: bridging language paradigms without the traditional overhead of massive R&D teams.

## The Methodology: AI-Augmented Engineering

This project was built using **extensive AI-augmentation tools** to empower human vision and strategic design.

This is not "AI-generated code" in the traditional sense; it is **AI-amplified architecture**. The vision, the constraints, and the trade-offs are human; the execution is accelerated by AI as a force multiplier for complex system design. We don't just use AI—we embrace it as the unavoidable present of software engineering.

## Installation

See [docs/INSTALLATION.md](docs/INSTALLATION.md) for installation procedures on Linux (.deb, .rpm, build from source), Windows, and macOS.

## Documentation

- [HPy User Guide](docs/HPY_USER_GUIDE.md): How to load and use HPy extensions.
- [HPy Developer Guide](docs/HPY_DEVELOPER_GUIDE.md): How to write HPy modules for protoPython.

## Author

Gustavo Marino <gamarino@gmail.com>

## License

MIT

---

## Lead the Shift

**Lead the Shift.** Don't just watch the Python ecosystem evolve—be the one who drives it. Join the review, test the GIL-less performance, and become part of the Swarm of One. **Think Different, As All We.**
