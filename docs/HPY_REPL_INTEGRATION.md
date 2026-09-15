# HPy and the REPL

> **Status (2026-09-15).** There is no HPy-specific integration in the `protopy` REPL.
> HPy-style extension modules are imported in the REPL like in scripts (see
> [HPY_USER_GUIDE.md](HPY_USER_GUIDE.md)). The REPL banner still ends with "[HPy Integrated]"; that text
> does not reflect this status.

Earlier versions of this page described a single `HPyContext` shared by the whole
environment, automatic closing of handles created during each REPL command and a
`%debug` command. None of these exist in the code:

- the loader creates a temporary `HPyContext` only for the duration of a module's
  `HPyInit_<name>` call (`src/library/HPyModuleProvider.cpp`);
- the REPL (`PythonEnvironment::runRepl`) has no handle management and no `%debug`
  command.

Reloading an extension module is not supported: the loader keeps each shared library
open until the process exits.
