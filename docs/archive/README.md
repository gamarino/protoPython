# Archive

This directory keeps dated documents that no longer describe the current state of
protoPython: superseded plans, roadmaps and status logs. They are preserved for
reference with their original text, apart from a banner at the top and relative links
repaired after the move. They are not maintained and may not match the current code;
some mention internal task lists and planning notes that are not part of the
repository. Current documentation is indexed in [docs/README.md](../README.md).

| File | Why it is archived |
|------|--------------------|
| [CPYTHON_CONFORMANCE_HISTORY.md](CPYTHON_CONFORMANCE_HISTORY.md) | Reverse-chronological conformance log (V-numbered entries from V70 and the `test_descr.py` sweep rounds up to 2026-05-18), split out of [CPYTHON_CONFORMANCE.md](../CPYTHON_CONFORMANCE.md), which now holds only the current status. |
| [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) | Roadmap and step-by-step completion log whose status line dates from February 2026 (Phase 8 in progress). |
| [STABILITY_AND_UX_PLAN_100.md](STABILITY_AND_UX_PLAN_100.md) | Plan for a 100-step batch (v73–v77, steps 1385–1484) on stability and REPL usability; every item is still unchecked and the plan is not tracked any more. |
| [FULL_STUB_IMPLEMENTATION.md](FULL_STUB_IMPLEMENTATION.md) | Early phase summary of the standard-library stub work, written before the conformance rounds; see [STUBS.md](../STUBS.md) for the stub catalogue. |
| [PACKAGING_ROADMAP.md](PACKAGING_ROADMAP.md) | Early packaging and distribution roadmap (install layout, proposed wheel layout, venv notes; v55–v57). |
| [MULTITHREAD_CPU_BENCHMARK.md](MULTITHREAD_CPU_BENCHMARK.md) | Describes an earlier `multithread_cpu` workload (4 × `sum(range(50_000))`) and a protoCore allocator debugging log; the current [benchmarks/multithreaded_cpu.py](../../benchmarks/multithreaded_cpu.py) runs a different workload. |
| [design-specs/](design-specs/README.md) | Design specifications written during development (April 2026). |
