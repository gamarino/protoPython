# Step #2 — Dispatch loop investigation

**Date**: 2026-06-15
**Conclusion**: NULL RESULT. No code change required.

## Hypothesis (from the diagnosis document)

The diagnosis ranked #2 as "hoist the computed-goto dispatch table" — mirror of protoJS commit `b989e88a` which moved the 256-entry dispatch table from a per-`runBytecode`-entry initialiser (256 default-fills + ~210 specific overrides per call) to a function-scope `static const void*[256]` with DCLP-style one-time init. The estimated impact was **−40 to −60 % on `executeBytecodeRange` self-time**, the single largest available win.

## Verification

### protoPython does NOT have the protoJS shape

protoPython's bytecode loop is a single C++ `switch (op)` statement (line 3952 of `src/library/ExecutionEngine.cpp`, closing at line 9588). With `-O3 -DNDEBUG -flto` (the release build), gcc compiles this switch into a `.rodata`-resident jump table that lives ONCE per process. Each per-opcode dispatch becomes a single indirect jump (`jmp *%rax`):

```bash
$ objdump -d build-lto/src/library/libprotoPython.so \
    | awk '/executeBytecodeRange/{p=1} p && /jmp.*\*/{print; if(++n>=5)exit}'
   2c3be:  ff e0   jmp    *%rax
   2f80c:  ff e0   jmp    *%rax
   5622f:  ff e0   jmp    *%rax
   56270:  ff e0   jmp    *%rax
   56eb6:  3e ff e0   notrack jmp *%rax
```

So protoPython pays zero per-call dispatch-table initialisation cost. The protoJS fix targeted code shape protoPython does not have. The cost protoJS shed (~470 stores per `runBytecode` entry × N entries) does not exist on protoPython's side.

### Why is `executeBytecodeRange` still 29 % of `call_recursion`?

After step #4 lands (diagEnabled constexpr-false), the perf profile for `fib(25)` shows:

```
 29.20 % protoPython::executeBytecodeRange (self-time)
 14.54 % __tls_get_addr
  4.53 % proto::ProtoContext::ProtoContext
  ...
```

The 29 % self-time is the *case body* execution — the actual work of LOAD_FAST, BINARY_ADD, COMPARE_OP, POP_JUMP_IF_FALSE, CALL, RETURN_VALUE that fib(25) repeats 121 K × 30 times. The dispatch itself — the indirect `jmp *%rax` — is bounded by the branch-predictor budget (3.94 % branch-miss rate from `perf stat`, in line with industry numbers for a switch-based dispatcher).

Within the case bodies, the cycle distribution after step #4 is:

- The TLS-keyed `getPyThread` lookup that every operand-stack manipulation triggers (14.5 % of total → addressed by #3).
- The fresh `ProtoContext` constructor/destructor pair around each Python call (4.5 % + 3.1 % = 7.6 % combined → addressed by #1).
- Per-name attribute access for LOAD_ATTR / STORE_ATTR (~5 % from `getAttribute` + helpers → addressed by #3 and downstream).
- Allocator slow paths for everything not on the thread freelist (~4 % from `ProtoSpace::getFreeCells` → addressed indirectly by #1 and #6 reducing pressure).

In short: **the dispatch loop's "self-time" is the sum of every opcode's body work, NOT a per-opcode dispatch overhead**. The leverage moves to wherever those bodies spend their time. The remaining optimisations (#1, #3, #5, #7) each address a fraction of that 29 %.

### Why the per-opcode try/catch stays in place

Each iteration is wrapped in `try { switch (op) { … } } catch (…)`. Lifting the try/catch up to the outer `for` was considered. It would save the per-iteration setjmp-style setup, but `executeBytecodeRange` recovers from exceptions and continues:

```cpp
} catch (const std::exception& e) {
    if (env && !env->hasPendingException()) {
        env->raiseRuntimeError(ctx, "internal C++ exception: " + e.what());
    }
}
// ...continues to the next opcode, with pending exception set...
i = next_i;
```

Lifting the try/catch out of the loop would change semantics: an exception in any opcode would unwind the entire range instead of being absorbed and propagated through the regular Python-level exception path. That is a correctness break, not a perf change.

## What this commit ships

This commit is documentation only. It records the investigation so a future reader looking at the diagnosis doc's #2 row does not waste time chasing the same hypothesis.

The leverage available on the dispatch loop has moved to:

- step #3 — cache `getPyThread` on a ProtoContext slot (14.5 % of post-#4 wall-clock).
- step #1 — bring the ProtoContext ctor cost (7.6 %) down via the SBO investigation.
- step #5 — rate-limit per-opcode `hasPendingException` checks (small but easy).
- step #7 — single-allocation argsList in `runUserFunctionCall` (mirror of protoJS P-JS-5).

## Reproducing the verification

```bash
cmake -B build-lto -S . -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build-lto -j
objdump -d build-lto/src/library/libprotoPython.so \
    | awk '/executeBytecodeRange/{p=1} p && /jmp.*\*/{print; if(++n>=5)exit}'
```
