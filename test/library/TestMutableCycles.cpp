// What conformance rule 13 measures, measured directly and BY SHAPE.
//
// WHY THIS IS A SEPARATE TEST AND NOT JUST THE CONFORMANCE CASE.  Rule 13's
// verdict is one comparison, `found > declared`, and it is deliberately blind to
// WHICH cycles it found -- protoCore cannot tell a captured `var` that refers to
// itself from a diagnostic back-pointer nobody needed, which is exactly why the
// rule turns on a declaration.  A change that replaced protoPython's cycle shapes
// with different ones of the same count would not move the rule at all.  This test
// is where the shapes are pinned.
//
// AND WHY A COUNT ALONE WOULD NOT DO.  Measured on this runtime, the number of
// cycles is NOT MONOTONE in retention.  A bare environment reports 105 cycles, 97 of
// them one-handle `__mro__` self-loops; running the probe takes the mutables table
// from 175 to 219 entries and the cycle count DOWN to 17, because subclassing links
// what were 97 separate components into one component that the detector reports as a
// single cycle.  More retention, fewer cycles.  (Independently, `protopy` on a `pass`
// program reports 177 handles / 107 cycles and on the probe 220 / 18, through
// PROTOCORE_MUTABLE_CYCLE_CHECK -- the same effect, two entries apart because that
// scan runs at space teardown.)  So this test asserts on the two quantities that do
// behave: which SHAPES are present, and how many mutable HANDLES sit inside a cycle.
//
// IT DOES NOT ASSERT "GREATER THAN ZERO".  Each shape below is traced to the one
// construct in the probe that creates it, and asserted at that count: one
// zero-arg `super()` call site gives exactly one self-bound `__getattr__`
// MethodCell, one closure gives exactly one `__closure_frames__` cycle.
//
// AND IT COUNTS THE WINDOW SEPARATELY FROM THE FINDING.  `findMutableCycles` can
// legitimately report zero, and zero means two unrelated things: "the graph is
// acyclic" and "nothing this runtime built was in the graph".  The mutables-table
// growth is asserted on its own, before any cycle count is read, so the two can
// never be confused -- the same distinction rule 13 itself draws between Pass and
// NotApplicable.

#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "ConformanceHost.h"

namespace {

// Cycles whose reported path contains `hop`.  `path` is a SHORTEST closed walk
// through the lowest-numbered handle of the component, so a shape is named by the
// attribute that closes that walk.
unsigned long countShape(const proto::MutableGraphReport& r, const char* hop)
{
    unsigned long n = 0;
    for (const proto::MutableCycle& c : r.cycles)
        if (c.path.find(hop) != std::string::npos) ++n;
    return n;
}

// The quantity that actually measures retention: how many mutable handles sit
// inside SOME cycle, and are therefore marked for the life of the space.  Unlike
// the cycle count this cannot fall when two components merge.
unsigned long handlesInCycles(const proto::MutableGraphReport& r)
{
    unsigned long n = 0;
    for (const proto::MutableCycle& c : r.cycles) n += (unsigned long) c.refs.size();
    return n;
}

}  // namespace

TEST(MutableCycles, TheProbesShapesArePresentAndAttributable)
{
    protoPython::PythonEnvironment env(STDLIB_PATH);
    proto::ProtoContext* ctx = env.getContext();
    ASSERT_NE(ctx, nullptr);
    proto::ProtoSpace* space = env.getSpace();
    ASSERT_NE(space, nullptr);

    const proto::MutableGraphReport before = space->findMutableCycles(ctx);
    ASSERT_FALSE(before.truncated)
        << "the baseline scan ran out of cell budget, so nothing below is a "
           "measurement of a complete graph";

    // Three of the four shapes must be ABSENT here, or the counts after the probe
    // are not attributable to the probe.  This is the half of the measurement that
    // a bare "> 0" would skip.
    EXPECT_EQ(countShape(before, ".__getattr__"), 0u)
        << "a self-bound __getattr__ MethodCell exists before the probe runs, so "
           "the count after it cannot be credited to the probe's super() call";
    EXPECT_EQ(countShape(before, ".__closure_frames__"), 0u)
        << "a __closure_frames__ cycle exists before the probe runs, so the count "
           "after it cannot be credited to the probe's closure";
    EXPECT_EQ(countShape(before, ".f_locals"), 0u)
        << "an f_locals self-cycle exists before the probe runs, so the count "
           "after it cannot be credited to the probe's class bodies";
    // The fourth is the bootstrap's own, and the probe can never be credited with
    // it: every built-in prototype holds its own __mro__.
    EXPECT_GT(countShape(before, ".__mro__"), 0u)
        << "the bootstrap prototypes no longer hold their own __mro__, which would "
           "be a real change to what rule 13 measures in this runtime";

    ASSERT_EQ(env.executeString(
                  protoPython::conformance::PythonConformanceHost::mutableProbeSource(),
                  "<mutable-cycles>"),
              0)
        << "the probe source did not execute, so no shape below was built";

    const proto::MutableGraphReport after = space->findMutableCycles(ctx);
    ASSERT_FALSE(after.truncated)
        << "the scan ran out of cell budget: the ABSENCE of a cycle is not "
           "established and no count below can be read as complete";

    // --- the window, on its own, before any cycle count is read --------------
    EXPECT_GT(after.handles, before.handles)
        << "the mutables table did not grow while the probe ran (" << before.handles
        << " -> " << after.handles << " entries), so the probe built no mutable and "
           "every count below is a statement about protoPython's bootstrap rather "
           "than about the shapes this test names";

    // --- the finding ---------------------------------------------------------
    std::printf("[ CYCLES   ] mutables table %lu -> %lu entries; cycles %lu -> %lu; "
                "handles inside a cycle %lu -> %lu; %lu cells walked\n",
                before.handles, after.handles,
                (unsigned long) before.cycles.size(),
                (unsigned long) after.cycles.size(),
                handlesInCycles(before), handlesInCycles(after), after.cellsVisited);
    for (const char* hop : {".__mro__", ".f_locals", ".__getattr__",
                            ".__closure_frames__"})
        std::printf("[ CYCLES   ]   %-22s %lu -> %lu\n", hop,
                    countShape(before, hop), countShape(after, hop));
    std::fflush(stdout);

    // One zero-arg `super()` call site in the probe, so exactly one self-bound
    // MethodCell cycle on the proxy make_super_proxy builds.  It installs three
    // self-bound cells (BuiltinsModule.cpp:4283, :4284, :4287) and the detector
    // reports one hop per component, the lowest-numbered one, so all three collapse
    // into a single cycle LABELLED `.__getattr__`.
    //
    // THIS CYCLE IS AVOIDABLE, and the expected value here is 1 only because the
    // removal has not landed.  Binding all three cells to nullptr leaves the whole
    // 651-case suite passing except this test -- the receiver reaches
    // py_super_getattr through the OBJ-level `__py_getattr_handler__` fast path
    // (PythonEnvironment.cpp:24785-24789), which passes it explicitly, so
    // asMethodSelf is never the channel.  docs/CONFORMANCE.md rule 13 records the
    // experiment.  When that fix lands, this expectation becomes 0 -- which is the
    // point of pinning it: the removal will be recorded here rather than being
    // silent.
    EXPECT_EQ(countShape(after, ".__getattr__"), 1u)
        << "the super() proxy's self-bound MethodCell cycle is no longer present "
           "exactly once.  If it is 0 because the cells were un-self-bound, that is "
           "the retention fix docs/CONFORMANCE.md rule 13 filed: change this "
           "expectation to 0 and close that entry";

    // One closure in the probe, so exactly one `__closure_frames__` cycle:
    // createUserFunction installs the defining frame on the function
    // (ExecutionEngine.cpp:1311-1324) and OP_STORE_NAME binds the function back
    // into that frame under its own name.
    EXPECT_EQ(countShape(after, ".__closure_frames__"), 1u)
        << "the closure the probe builds no longer holds its defining frame.  "
           "OP_LOAD_DEREF walks __closure_frames__ at ExecutionEngine.cpp:6361, so "
           "if this edge is genuinely gone the free variable must resolve some "
           "other way and that path needs its own test";

    // The probe runs two class bodies, each of which aliases its namespace object
    // as its own f_locals (ExecutionEngine.cpp:8740).  Measured as one cycle, not
    // two, because the namespaces end up in one component; bounded rather than
    // fixed for that reason.
    EXPECT_GE(countShape(after, ".f_locals"), 1u)
        << "no class-body namespace aliases itself as f_locals any more";
    EXPECT_LE(countShape(after, ".f_locals"), 4u)
        << "more f_locals self-cycles than the probe has class bodies and frames";

    // Retention, measured as handles rather than cycles.  Measured here: the table
    // goes 175 -> 219 entries (+44) and the handles inside some cycle go 125 -> 175
    // (+50).  The bound is set at +72, a little under 1.5x the measurement, because
    // what it has to catch is a new retention SHAPE and not a few handles either
    // way.  Each handle in a cycle is permanent: the mutables table is a GC root
    // unconditionally and an entry is released only once its handle has been
    // finalized, so a value that reaches its own handle is marked for ever and a
    // later collection does not fix it.
    EXPECT_LE(handlesInCycles(after), after.handles)
        << "more handles counted inside cycles than exist in the table";
    EXPECT_LE(handlesInCycles(after), handlesInCycles(before) + 72u)
        << "the probe put " << (handlesInCycles(after) - handlesInCycles(before))
        << " further mutable handles into a cycle, against the "
        << (after.handles - before.handles) << " mutables it added to the table";
}
