# Contributing to ProtoPython

## Project Structure and File Organization

### Test Files and Temporary Artifacts

To maintain a clean root directory and organized workspace, the following rule applies to all contributors and automated processes:

> [!IMPORTANT]
> **All test-related files, ad-hoc test scripts, verification outputs, and temporary diagnostic files MUST reside in the `tests/` directory.**

- **Do not** create new `test_*.py` or `verify_*.py` files in the root directory.
- **Do not** store `.txt` or `.log` output files from tests in the root directory.
- Use the `tests/` directory for any experimental or diagnostic scripts.

### CPython Conformance

When working on CPython grammar conformance, place specific grammar tests in `test/cpython/` if they are part of the standard suite, or in `tests/` if they are ad-hoc reproductions of issues.
