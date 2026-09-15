# F4.5 — Per-function severity inventory: OsModule / ThreadModule / MathModule

Scope: classify every callable registered via `setAttribute(ctx, "name", ctx->fromMethod(...))` in
the three target files. STUB = body is essentially `return PROTO_NONE` (or a constant) regardless of
inputs; PARTIAL = real work but observable semantic gap vs CPython; REAL = substantive impl.
Severity applies only to STUB/PARTIAL.

The headline finding: the previous audit's PROTO_NONE counts included **domain-error guards** (e.g.
`sqrt(-1) -> PROTO_NONE`). Most PROTO_NONE returns in MathModule are guards, not stubs. The real
silent-failure surface is in **OsModule's "swallow syscall error" pattern** and a handful of
ThreadModule policy stubs.

---

## OsModule (`src/library/OsModule.cpp`)

### `os.path` / `os.scandir` / DirEntry helpers

| Name | Class | Severity | Reason |
|---|---|---|---|
| DirEntry.is_dir | REAL | — | calls stat(); returns S_ISDIR. |
| DirEntry.is_file | REAL | — | calls stat(); returns S_ISREG. |
| DirEntry.is_symlink | REAL | — | calls lstat(); returns S_ISLNK. |
| DirEntry.stat | REAL | — | calls stat(); raises OSError on failure. |
| DirEntry.inode | REAL | — | calls lstat(); raises OSError on failure. |
| DirEntry.__fspath__ | PARTIAL | LOW | returns PROTO_NONE if `path` attr missing (rare). |
| scandir __next__ / __iter__ | REAL | — | proper readdir state machine; pinned with finalizer. |
| stat_result.__getitem__ | REAL | — | indexes the 10-tuple of fields. |

### `os.environ` (delegates to getenv/setenv)

| Name | Class | Severity | Reason |
|---|---|---|---|
| environ.__getitem__ / __setitem__ / __delitem__ | REAL | — | thin wrappers around getenv/setenv/unsetenv. |
| environ.__iter__ | REAL | — | iterates `environ_keys`. |
| environ.keys / values / items | REAL | — | walks libc `environ` array. |

### `os.*` core

| Name | Class | Severity | Reason |
|---|---|---|---|
| getenv | REAL | — | std::getenv with default. |
| putenv (also `os.environ` setter) | PARTIAL | MEDIUM | calls setenv() but never raises on failure; CPython raises OSError. |
| unsetenv | PARTIAL | LOW | calls unsetenv() ignoring return code. |
| getcwd | REAL | — | getcwd() with 4 KiB buffer. |
| readlink | PARTIAL | MEDIUM | on failure returns the input path (not an error); silently wrong for callers that probe `is_symlink`. |
| chdir | PARTIAL | HIGH | always returns None even on failure; CPython raises FileNotFoundError. Common in test suites that chdir to scratch dirs. |
| listdir | REAL | — | opendir/readdir loop, skips `.`/`..`. |
| scandir | PARTIAL | MEDIUM | returns PROTO_NONE on opendir failure instead of raising FileNotFoundError; iterators that expect raise will silently halt. |
| stat | REAL | — | raises OSError on failure via env->raiseOSError. |
| lstat | REAL | — | as stat. |
| remove | PARTIAL | HIGH | calls unlink() but ignores result; silent success on missing files breaks `tempfile`/test cleanup expecting FileNotFoundError. |
| unlink | PARTIAL | HIGH | alias of remove → same bug. |
| mkdir | PARTIAL | HIGH | swallows mkdir() error; CPython raises FileExistsError when dir exists. Breaks `os.makedirs` retry logic. |
| rename | PARTIAL | MEDIUM | swallows rename() error; CPython raises OSError. |
| replace | PARTIAL | MEDIUM | alias of rename → same. |
| access | REAL | — | direct access() syscall. |
| rmdir | PARTIAL | MEDIUM | swallows rmdir() error; CPython raises OSError. |
| getuid / geteuid / getgid / getegid | REAL | — | direct libc call. |
| environ_keys | REAL | — | walks libc `environ`. |
| waitpid | PARTIAL | LOW | calls waitpid() but does not raise ChildProcessError on -1; returns (-1, 0). |
| waitstatus_to_exitcode | PARTIAL | LOW | returns None when neither WIFEXITED nor WIFSIGNALED; CPython raises ValueError. |
| WIFSTOPPED | REAL | — | macro evaluation. |
| WSTOPSIG | REAL | — | macro evaluation. |
| urandom | PARTIAL | MEDIUM | reads /dev/urandom but **rewrites NUL bytes with rand()** to avoid ProtoString truncation — output is biased and not cryptographically random; comment in source acknowledges the kludge. |
| kill | PARTIAL | MEDIUM | calls kill() but ignores return; no PermissionError on EPERM. |
| cpu_count | REAL | — | std::thread::hardware_concurrency. |
| pipe | REAL | — | pipe() with proper tuple result; returns None only on syscall failure (could raise). |
| _exit | REAL | — | direct exit(). |
| open | PARTIAL | HIGH | returns PROTO_NONE on open() failure instead of raising FileNotFoundError/PermissionError; downstream `os.read(None)` -> "'NoneType' has no attribute…". Callers commonly test `try: os.open(...) except OSError`. |
| close | PARTIAL | MEDIUM | swallows close() error; CPython raises OSError on EBADF. |
| utime | PARTIAL | LOW | utimensat with full kwargs handling; on failure silently returns None (comment acknowledges this is intentional vs CPython's OSError). |
| isatty | REAL | — | direct isatty(). |
| getpid / getppid | REAL | — | direct libc. |
| _path_splitroot_ex | REAL | — | full POSIX root-split impl. |
| _path_normpath | STUB | LOW | passthrough — returns the input unchanged. Mitigated by `lib/python3.14/posixpath.py` which doesn't rely on this; only `pathlib._abc` uses it directly. |
| _create_environ | REAL | — | builds dict from libc environ. |

### Top-5 OsModule fixes (highest severity)

1. **`os.open`** — return real OSError so `try/except` works; current behavior produces `'NoneType' has no attribute X` errors far downstream.
2. **`os.mkdir`** — raise FileExistsError; `os.makedirs(exist_ok=False)` and tempfile.mkdtemp depend on this.
3. **`os.remove` / `os.unlink`** — raise FileNotFoundError; test fixtures that delete-then-recreate hide bugs without it.
4. **`os.chdir`** — raise FileNotFoundError on failure; pytest/unittest fixture errors silently leave you in the wrong dir.
5. **`os.urandom`** — replace NUL rewrite kludge with a real `bytes` carrier (the bytes prototype already exists). Cryptographic correctness.

---

## ThreadModule (`src/library/ThreadModule.cpp`)

| Name | Class | Severity | Reason |
|---|---|---|---|
| Lock.acquire | REAL | — | std::mutex lock / try_lock with `held` flag. |
| Lock.release | REAL | — | unlocks underlying mutex; sets held=false. |
| Lock.locked | REAL | — | returns held flag. |
| Lock.__enter__ / __exit__ | REAL | — | trampoline to acquire/release. |
| RLock.acquire | REAL | — | std::recursive_mutex with count. |
| RLock.release | REAL | — | decrement + unlock. Note: **does not check current owner** so release-without-acquire from another thread is silently UB; CPython raises RuntimeError. |
| RLock.locked | REAL | — | count > 0. |
| RLock.__enter__ / __exit__ | REAL | — | trampoline. |
| start_new_thread | REAL | — | space->newThread with bootstrap. |
| join_thread | REAL | — | thread->join(). |
| is_alive | REAL | — | checks ProtoSpace.threads sparse list. |
| get_ident | REAL | — | current_thread_id(). |
| getpid | REAL | — | duplicated with os.getpid; harmless. |
| log_thread_ident | STUB | LOW | body is literally `return PROTO_NONE; return PROTO_NONE;`. Diagnostic only; nothing in stdlib calls it. |
| allocate_lock / allocate_rlock | REAL | — | builds child of `_lockProt` with ExternalPointer. |
| _lock_acquire / _lock_release / _rlock_acquire / _rlock_release / RLock / LockType | REAL | — | aliases / factories. |
| _is_main_interpreter | STUB | LOW | always returns PROTO_TRUE; protoPython has no subinterpreter feature so semantically correct. |
| _get_main_thread_ident | REAL | — | returns g_main_thread_id. |
| start_joinable_thread | REAL | — | builds handle, spawns thread, populates ident/_proto_thread. |
| daemon_threads_allowed | STUB | MEDIUM | always returns TRUE; should reflect `sys.flags.dev_mode` / shutdown state per CPython. Affects threading._shutdown ordering when daemons are present. |
| _shutdown | STUB | MEDIUM | no-op; CPython's `_thread._shutdown` joins all non-daemon threads. Programs that rely on automatic join-at-exit may exit before worker threads finish. |
| _make_thread_handle | REAL | — | constructs handle object with is_done/join/_set_done methods. |
| _ThreadHandle | REAL | — | factory alias. |
| _count | REAL | — | space->runningThreads.load(). |
| _get_thread_handle | REAL | — | indexes space->threads sparse list. |
| handle.is_done | REAL | — | checks `_done` attr or thread sparse-list slot. |
| handle.join | REAL | — | thread->join(). |
| handle._set_done | REAL | — | sets `_done` attr. |

### Top-5 ThreadModule fixes

1. **`_shutdown`** — without it, the main interpreter can exit while worker threads are mid-write; programs using `threading.Thread(target=...)` without explicit `.join()` lose output silently.
2. **`daemon_threads_allowed`** — currently always TRUE; threading.py's shutdown machinery branches on this and may skip joins.
3. **`RLock.release` ownership check** — silent UB on cross-thread release; one-line `if (ld->owner != current_thread) raise RuntimeError`.
4. **`log_thread_ident`** — remove the dead double-return; either implement (one log line) or drop the registration.
5. **`_is_main_interpreter`** — fine for now, but document that subinterpreters are unsupported so users don't expect Python 3.12+ isolation behavior.

---

## MathModule (`src/library/MathModule.cpp`)

All ~60 registered functions delegate to `std::` (sqrt, sin, cos, log, exp, gamma, fma, gcd, lcm,
nextafter, frexp, modf, etc.) with per-function domain guards. No stubs found.

| Name | Class | Severity | Reason |
|---|---|---|---|
| sqrt sin cos tan asin acos atan atan2 | REAL | — | std::* trig. |
| degrees radians floor ceil fabs trunc copysign | REAL | — | direct delegation. |
| isclose isinf isfinite isnan | REAL | — | full IEEE-754 semantics. |
| log log10 log2 log1p | REAL | — | with domain guards. |
| hypot fmod remainder erf erfc gamma lgamma exp | REAL | — | direct std::* delegation. |
| dist | REAL | — | unwraps `__data__` Lists, sums squared diffs. |
| perm comb factorial | PARTIAL | MEDIUM | use `long long` accumulator → silent overflow at ~21! (CPython uses arbitrary-precision int). |
| prod | PARTIAL | MEDIUM | accumulates as **double** even for integer inputs; `math.prod([10**18, 10**18])` loses precision. CPython promotes to int. |
| sumprod isqrt acosh asinh atanh cosh sinh tanh ulp nextafter | REAL | — | std::* delegation. |
| ldexp frexp modf | REAL | — | proper tuple results. |
| cbrt exp2 expm1 fma | REAL | — | direct std::* (C++17). |
| gcd lcm | PARTIAL | LOW | `long long` accumulator; multi-arg form (Python 3.9+) not supported (only 2 args). |

Domain-error guards (e.g. `sqrt(-1) -> PROTO_NONE`) deviate from CPython which raises ValueError /
ZeroDivisionError. **Severity: MEDIUM across the board** — silently returning None for math errors
is the canonical "where did this NoneType come from" bug pattern. A single error-translation
helper applied to every guard site would fix it without touching the math.

### Top-5 MathModule fixes

1. **Domain-error guards return None instead of raising ValueError** — affects ~30 functions uniformly; one helper macro is enough.
2. **`math.prod` accumulator is double** — silent precision loss for integer iterables; should branch on element type.
3. **`math.factorial` / `math.perm` / `math.comb` overflow at long-long limits** — needs big-int path or explicit OverflowError.
4. **`math.gcd` / `math.lcm` only accept 2 args** — Python 3.9+ accepts variadic; user code using `gcd(a,b,c)` silently calls wrong overload (likely "0 args after self" → returns PROTO_NONE).
5. **`math.isqrt` uses `sqrt(double)` round-down** — loses precision for n > 2^53; should use Newton iteration on long long.

---

## Cross-cutting recommendation

The dominant failure mode across all three modules is **silent error swallowing**: a syscall fails
or a domain check fires, and the function returns `PROTO_NONE` instead of raising the corresponding
exception. The user sees a downstream `'NoneType' has no attribute X` error from the line that
consumes the result, often in stdlib code far from the original call.

A single helper — `raisePosixError(ctx, errno, path)` already exists in OsModule (`env->raiseOSError`)
and is used by `stat`/`lstat`/DirEntry only — applied uniformly would fix the OsModule HIGH items
in a handful of lines. A parallel `raiseValueError(ctx, msg)` for MathModule's domain guards would
close the math gap.

Estimated effort to land all 15 HIGH/MEDIUM fixes across the three modules: 4–6 hours.
