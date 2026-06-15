# 2026-06-15 — protoPython optimisation pass: final comparison

This document closes the optimisation sprint that started with `docs/2026-06-15-overhead-diagnosis.md`. Seven commits landed; this one reports the comparative numbers.

## Setup

- **Hardware**: Ryzen 5500U (6 physical cores, SMT 8, mobile/laptop class), Linux x86_64.
- **Date**: 2026-06-15.
- **Methodology**: median of 7 runs, all measurements serial (no concurrent load).
- **protoCore**: `libprotoCore.so.1.2.0` — same binary for `protopy v0`, `protopy v1`, and the protoCpp comparators.
- **CPython**: system `python3` (Debian package).
- **Bench sources**: `protoPython/benchmarks/{int_sum_loop,call_recursion,attr_lookup,list_append_loop,str_concat_loop}.py` — unchanged across the comparison.

`protopy v0` is `9c4f1578` (the commit right before this optimisation pass started). `protopy v1` is `ac4a2505` (after #1, #3, #4, #5, #7 landed; #2 and #6 deferred with documented null/architectural reasons).

## The table

| benchmark           | CPython (ms) | protopy v0 (ms) | **protopy v1 (ms)** | Δ v1 vs v0 | v1/CPython |
|---|---:|---:|---:|---:|---:|
| `int_sum_loop`      |  39.53 |    50.25 |    **38.37** |  −24 % |   **0.97× (parity)** |
| `call_recursion`    |  53.19 |  2704.40 |   **109.40** |  **−96 %** |   2.06× |
| `attr_lookup`       |  49.15 |   432.97 |   **207.25** |  −52 % |   4.22× |
| `list_append_loop`  |  44.57 |   501.68 |   **297.12** |  −41 % |   6.67× |
| `str_concat_loop`   |  42.36 |   528.09 |   **363.63** |  −31 % |   8.59× |

## What each commit contributed

The commit-by-commit deltas are recorded in each individual commit message; this is a summary.

| step | commit | what | bench impact |
|---|---|---|---|
| `#4` | `82a5dd08` | `diagEnabled()` etc. constexpr-false in NDEBUG | the headline win: enabled the optimiser to dead-code-eliminate every `if (diagXxxEnabled()) { … }` block. **call_recursion went 2704 → 203 ms in this one commit.** Other benches: −15 to −32 %. |
| `#2` | `fc3600c9` | dispatch table investigation | NULL result. Verified that gcc already produces `jmp *%rax`-style indirect dispatch for the giant `switch (op)`. The protoJS-style fix doesn't apply. |
| `#3 + #5` | `4ceb8694` | `-ftls-model=initial-exec` on libprotoPython | every `thread_local` (s_threadEnv, s_threadContext, s_pendingExcFlag, s_currentPyThread, …) becomes a single `mov %fs:offset, %reg` instead of a `__tls_get_addr` libc call. **call_recursion 203 → 116 ms** (an additional −43 %). |
| `#1` | `c011dee9` | `ContextScope SBO_SLOTS 64 → 256` | Mirror of protoJS commit `b989e88a`. The previous 64-slot stack buffer was too small for any Python function with a non-trivial argument list + locals + operand stack; spill paths fell back to heap. Bumped to 256. **−7 to −16 %** across all benches. |
| `#7` | `ac4a2505` | `ctx->newList(N, items)` instead of `newList() + N×appendLast` in the non-fast-path call | Mirror of protoJS P-JS-5. Effect within run-to-run noise on these benches because fib / int_sum / attr_lookup all hit the fast path that skips this entirely. The change is correct, just invisible on the microbench. |
| `#6` | `57c5383c` | list-mutable-when-owned | DEFERRED. The intended fix requires a `ProtoMutableList` backend at the protoCore level — a kernel design RFC, not a protopy patch. Documented as such. |

## What the comparison means

### `int_sum_loop` — parity with CPython

`int_sum_loop` is the SmallInt fast-path benchmark; it was *already* the closest protopy bench to CPython at v0 (1.27× over). With the housekeeping costs (TLS, diagnostics, slot allocation) collapsed, it now sits at **0.97× — within measurement noise of CPython on this hardware**. Future runs on a quieter machine may go either side of parity. The kernel + a competent interpretation layer is enough to keep up with CPython here.

### `call_recursion` — the 25× speedup

The recursive Python call was the worst bench in protopy v0: 2704 ms vs CPython's 53 ms, a **51.5× ratio**. Most of that was per-call housekeeping: thread-local reads in `s_currentFrame`/`s_currentGlobals`/`s_currentCodeObject`, a fresh `ProtoContext` per call with too-small SBO, and inside every opcode body — including the leaf opcodes — an `if (diagXxxEnabled())` branch that the compiler had to honour because `diagEnabled()` returned a runtime value.

After the seven-step pass, protopy v1 runs `call_recursion` in **109 ms — a 25× speedup over v0**, and **2.06× CPython on this hardware**. That is well within the regime where a JIT or AOT pass closes the rest of the gap.

### Where the remaining gap lives

`attr_lookup`, `list_append_loop`, `str_concat_loop` all sit at 4-9× CPython on this hardware after v1. The residual cost is now mostly in the kernel's persistent data structures:

- Every `obj.x` read goes through `ProtoObject::getAttribute` + the per-thread `AttributeCache` (which IS already in protoCore).
- Every `lst.append(item)` rebuilds a new persistent AVL spine instead of mutating in place.
- Every `s + "x"` allocates a fresh rope node.

These are the cost of protoCore's "immutability by default" guarantee, which is part of the kernel's design contract. Closing the remaining gap is no longer a protopy-side patch; it requires the `ProtoMutableList` RFC documented in `docs/2026-06-15-step-6-list-mutable-deferred.md`.

## protoCpp context

Running the same algorithms through protoCore directly in C++ on the same hardware:

| benchmark           | protoCpp (ms) | v1 vs protoCpp |
|---|---:|---:|
| `int_sum_loop`      |  16.32 |  2.35× |
| `call_recursion`    |  38.55 |  2.84× |
| `attr_lookup`       |  21.46 |  9.66× |
| `list_append_loop`  |  17.95 | 16.55× |
| `str_concat_loop`   |  17.80 | 20.43× |

The Python interpretation layer adds 2-20× over the kernel cost depending on workload. The wider gap on `list` / `str` is the same persistent-structure cost noted above, amplified by Python's per-iteration opcode dispatch. The narrower gap on `int_sum` / `call_recursion` is where the optimisation pass focused — the housekeeping that the kernel forced protopy to do on every dispatch.

## Reproducing

```bash
# Build protoCore and protoPython at the relevant commits.
cd protoCore && cmake -B build_release -S . && cmake --build build_release --target protoCore
cd ../protoPython
git checkout 9c4f1578           # protopy v0
cmake -B build-lto-v0 -S . -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build-lto-v0 -j

git checkout ac4a2505           # protopy v1
cmake -B build-lto -S . -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build-lto -j

# Bench.
for binary in build-lto-v0/src/runtime/protopy \
              build-lto/src/runtime/protopy \
              $(which python3); do
    echo "=== $binary ==="
    for b in int_sum_loop call_recursion attr_lookup \
             list_append_loop str_concat_loop; do
        echo -n "$b "
        for _ in 1 2 3 4 5 6 7; do
            t0=$(date +%s.%N)
            $binary benchmarks/$b.py > /dev/null 2>&1
            t1=$(date +%s.%N)
            awk -v a=$t0 -v b=$t1 'BEGIN { printf "%.2f\n", (b-a)*1000 }'
        done | sort -n | awk 'NR==4{print}'
    done
done
```

## Related

- The motivating bench suite that quantified the kernel vs language-layer split: <https://github.com/gamarino/protoCpp/blob/main/RESULTS.md>
- The diagnosis document that opened this sprint: `docs/2026-06-15-overhead-diagnosis.md`.
- The per-step null/deferred records: `docs/2026-06-15-step-2-dispatch-investigation.md`, `docs/2026-06-15-step-6-list-mutable-deferred.md`.
