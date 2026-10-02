# Cost of chaining every context onto the thread's current context (2026-10-02)

Question: what does `f84ec514` ("Chain every new context onto the thread's
current context", merged to `main` in `6ab71679`) cost on call-heavy code?
Every `ContextScope` (one per Python call) and `runCodeObject` now takes its
`previous` from `ctx->thread->getCurrentContext()` (`chainParent`,
`include/protoPython/MemoryManager.hpp`) instead of the context it was handed:
one load and one out-of-line call into libprotoCore per call.

## Method

- Builds: Release, the same installed protoCore (`/usr`, soname 3), each from
  a clean worktree: `c8ac3b2a` (`main` before the merge), `0d26f0d5` (the
  context-chain commit's parent), `f84ec514` (the commit), `6ab71679` (`main`
  after the merge, which also brings `__annotate__`, read-mode file
  `seek`/`tell`, absolute `__main__.__file__` and protopyc fixes).
- Harness: [`benchmarks/perf_stat_ab.py`](../perf_stat_ab.py):
  `perf stat -r 5 -e cycles,instructions` per binary and benchmark, binaries
  interleaved, two rounds, minimum of the two rounds reported. Whole process
  (start-up included, about 53 M instructions). Every run's result line is
  checked (fib(27) = 196418, fib(25) = 75025, richards total = 57900,
  nqueens(8) = 92, attr_lookup = 12000000); a run that crashes or computes
  something else aborts the measurement.
- Machine: DEV12 (AMD Lucienne, 6 cores / 12 threads), otherwise idle.

## Results

Relative to `0d26f0d5`, the context-chain commit's parent:

| benchmark | cycles `f84ec514` | instructions `f84ec514` | cycles `6ab71679` | instructions `6ab71679` |
|---|---:|---:|---:|---:|
| call_recursion fib(27) | −2.22 % | +0.63 % | −2.96 % | +0.74 % |
| pyperf fib(25) x6 | −3.01 % | +0.82 % | −7.09 % | +0.97 % |
| richards_lite 200x20 x6 | +1.63 % | +0.20 % | +0.92 % | +0.22 % |
| nqueens 8 | −0.09 % | −0.06 % | +3.31 % | +0.53 % |
| attr_lookup 2M | −5.75 % | −0.11 % | +0.38 % | +0.30 % |

`c8ac3b2a` (`main` before the merge) is within 0.05 % of `0d26f0d5` in
instructions on every benchmark. A first run of `c8ac3b2a` against `6ab71679`
alone gave the same picture: instructions +0.26 % to +0.99 %, cycles −1.5 % to
+1.5 %, except nqueens +4.81 %.

## Reading

- The context-chain change costs at most 0.82 % instructions (call-dominated
  fib), consistent with one extra out-of-line call per Python call. Its cycle
  differences go both ways by up to 6 % with under 1 % instruction change:
  code layout and noise, not work. It is under the 3 % threshold set for a
  rework, so `chainParent` is unchanged. A cheaper variant (a protoPython-side
  thread-local copy of the current context, updated on push and pop) would
  have to track every context protoCore and native code create, which is the
  class of bug the change removed; the authoritative pointer is protoCore's.
- nqueens is +3.3 % to +4.8 % cycles on `6ab71679` with +0.5 % instructions, and
  is unchanged on `f84ec514`: the difference comes from the other commits of
  the merge (no call-path change among them) and has the signature of a layout
  effect. Not investigated further.
- The `fix/stdin-syspath-perf` branch (`sys.stdin`, `sys.path`) measured
  against `6ab71679`: instructions −0.00 % to +0.43 %, cycles −2.96 % to
  +0.85 %.
