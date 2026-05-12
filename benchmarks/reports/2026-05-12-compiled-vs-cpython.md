# Benchmark: protoPython vs CPython (with protopyc compilation)

Date: 2026-05-12
Platform: Linux x86_64
Comparison: CPython 3 (system) vs protoPython interpreted vs protoPython compiled via `protopyc`

## Methodology

Four expression-level workloads that protopyc can compile correctly today (avoid `+=`, `while`, and other features the compiler does not yet lower fully):

| Workload      | Source |
|:--            |:--     |
| int_sum_50k   | `result = sum(range(50000))` |
| list_build_5k | `result = [x*2 for x in range(5000)]` |
| str_concat    | `result = "".join("x" for _ in range(10000))` |
| dict_build    | `result = {i: i*i for i in range(5000)}` |

Each ran 3 times via `/usr/bin/time -f '%e'`, median (middle of 3) reported in ms (includes process startup).

- **CPython**: `python3 file.py`
- **protopy (interpreted)**: `protopy file.py` — full source → AST → bytecode → ExecutionEngine each run
- **protopyc (compiled)**: `protopyc file.py --build-so` produces `module.so`; loaded via `run_module module.so`. The .so embeds a generated C++ stub that calls into the same protoPython runtime via `env->callObject` / `env->lookupName` / `env->storeName`, so opcode dispatch overhead is removed but every operation still goes through the runtime's polymorphic method tables.

## Results

| Benchmark      | CPython | protopy (interp) | protopyc (compiled) |
|:--             |    --:  |             --:  |               --:   |
| int_sum_50k    |  120 ms |          100 ms  |             50 ms   |
| list_build_5k  |  180 ms |          100 ms  |             50 ms   |
| str_concat     |  130 ms |           50 ms  |             80 ms   |
| dict_build     |  160 ms |          130 ms  |             60 ms   |

## Observations

- **Compiled is consistently faster than interpreted** on the int / list / dict workloads (~2x), confirming that the AST→bytecode→dispatch loop is the dominant overhead in those cases — protopyc folds it into a single C++ call sequence.
- **Compiled beats CPython** on all four workloads except str_concat.  The geomean speedup of protopyc over CPython on the four is roughly **2.0x**.
- **str_concat** shows protopy's interpreted path already beating CPython (50 ms vs 130 ms) because protoCore's string append uses structural sharing (rope-style) rather than copy-on-append.  The compiled path is slower here because the loop body still allocates one ProtoString per iteration, eliminating the rope-fold optimisation that the interpreter applies in `OP_BUILD_STRING`.
- **Startup overhead** (process launch + env init) is a non-trivial fraction of these tiny workloads.  The compiled `run_module` path includes the full environment initialisation just like `protopy`, so apples-to-apples within protoPython, but CPython's startup is lighter for empty programs.

## Caveats

- `protopyc` currently lowers a subset of the language: augmented assignment (`+=`), `while` loops, `for` loops with control flow, and several other constructs do not produce correct semantics.  The four benchmarks here were chosen specifically because they pass the `result == expected` correctness check on the compiled output.
- The `run_module` test harness was not built by CMake; I rebuilt it via `g++ -O3 -std=c++20 ... run_module.cpp ... -ldl` against the current shared libraries before this measurement.
- All workloads small enough that I/O / OS scheduling jitter dominates sub-10 ms variance; report at 10 ms granularity to avoid false precision.

## Conclusion

The `protopyc` compilation pipeline does what its name promises on the workloads it lowers correctly — removes the interpreter dispatch overhead and exposes the underlying protoCore runtime, yielding ~2x speedup over CPython 3 on simple expression-level benchmarks.  The catch is correctness: until the compiler reaches feature parity with the full bytecode interpreter, only a subset of programs can be measured this way.
