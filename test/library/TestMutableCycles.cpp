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
proto::proto_ulong countShape(const proto::MutableGraphReport& r, const char* hop)
{
    proto::proto_ulong n = 0;
    for (const proto::MutableCycle& c : r.cycles)
        if (c.path.find(hop) != std::string::npos) ++n;
    return n;
}

// The quantity that actually measures retention: how many mutable handles sit
// inside SOME cycle, and are therefore marked for the life of the space.  Unlike
// the cycle count this cannot fall when two components merge.
// Cycles closed by a super() proxy's own cells.  The proxy carries three
// (`__getattr__`, `__setattr__`, `__py_getattr_handler__`), and the detector
// reports ONE hop per cycle: the first hop of a shortest closed walk, found by
// a breadth-first search that visits the attribute tree in key order.  An
// attribute's key is its symbol's address, so WHICH of the three names labels
// the cycle depends on where the bootstrap happened to allocate three interned
// strings.  Counting a single name made this test flaky on Windows (CI on
// 2026-10-29: `.__getattr__` read 0 in two runs out of three, with the
// cycle count unchanged); the shape is counted under all three names.
proto::proto_ulong countSuperProxyShape(const proto::MutableGraphReport& r)
{
    proto::proto_ulong n = 0;
    for (const proto::MutableCycle& c : r.cycles)
        if (c.path.find(".__getattr__") != std::string::npos
            || c.path.find(".__setattr__") != std::string::npos
            || c.path.find(".__py_getattr_handler__") != std::string::npos) ++n;
    return n;
}

proto::proto_ulong handlesInCycles(const proto::MutableGraphReport& r)
{
    proto::proto_ulong n = 0;
    for (const proto::MutableCycle& c : r.cycles) n += (proto::proto_ulong) c.refs.size();
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
    EXPECT_EQ(countSuperProxyShape(before), 0u)
        << "a super() proxy cell cycle exists before the probe runs, so the "
           "count after it cannot be credited to the probe's super() call";
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
    std::printf("[ CYCLES   ] mutables table %llu -> %llu entries; cycles %llu -> %llu; "
                "handles inside a cycle %llu -> %llu; %llu cells walked\n",
                static_cast<unsigned long long>(before.handles), static_cast<unsigned long long>(after.handles),
                static_cast<unsigned long long>((proto::proto_ulong) before.cycles.size()),
                static_cast<unsigned long long>((proto::proto_ulong) after.cycles.size()),
                static_cast<unsigned long long>(handlesInCycles(before)), static_cast<unsigned long long>(handlesInCycles(after)), static_cast<unsigned long long>(after.cellsVisited));
    for (const char* hop : {".__mro__", ".f_locals", ".__getattr__",
                            ".__setattr__", ".__py_getattr_handler__",
                            ".__closure_cells__"})
        std::printf("[ CYCLES   ]   %-22s %llu -> %llu\n", hop,
                    static_cast<unsigned long long>(countShape(before, hop)), static_cast<unsigned long long>(countShape(after, hop)));
    std::fflush(stdout);

    // One zero-arg `super()` call site in the probe, and no cycle from it.
    // make_super_proxy used to bind its three cells to the proxy they are
    // stored on, one permanent cycle per super() call (28 % of the cycles a
    // census of the ctest run found, docs/CONFORMANCE.md rule 13).  The
    // cells are unbound now: the proxy reaches py_super_getattr as an
    // argument of the OBJ-level handler dispatch.  Counted under all three
    // cell names, since the label the detector picks is address-dependent
    // (countSuperProxyShape).
    EXPECT_EQ(countSuperProxyShape(after), 0u)
        << "a super() proxy is in a cycle through its own cells again: every "
           "super() call keeps its proxy, and the instance it holds, for ever";

    // One closure in the probe, and no cycle from it.  Until 2026-10-11 a
    // function held its defining frame (`__closure_frames__`) and that frame,
    // mapped because the closure captured one of its locals, held the
    // function: one cycle per call of the defining function.  A function now
    // holds the cells it closes over (`__closure_cells__`), and a cell holds a
    // value, never the frame or the function that created it.
    EXPECT_EQ(countShape(after, ".__closure_frames__"), 0u)
        << "a function still holds its defining frame";
    EXPECT_EQ(countShape(after, ".__closure_cells__"), 0u)
        << "the probe's closure is in a cycle through its cells";

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

// A function's closure frame binds the function's own name only when the
// function, or a scope nested in it, reads that name as a free variable (a
// recursive nested function), and never a name such as `<listcomp>` that no
// code can read.  OP_BUILD_FUNCTION used to bind it for EVERY function that
// keeps its closure frame, which closed a permanent cycle
//     closure frame -[.<listcomp>]-> fn -[.__closure_frames__]-> closure frame
// for every comprehension, generator expression and lambda that reads an
// enclosing variable: one per EXECUTION.  A loop running a list comprehension
// 20,000 times grew the live set by about 55 cells per iteration for ever
// (161 K -> 1.27 M cells over 189 cycles under the adaptive controller).
//
// Measured on handles inside cycles, not on a shape: the functions below are
// dropped after use, so nothing they build may stay in a cycle.  Named nested
// defs are covered by NamedNestedDefsLeaveNoCycle.
TEST(MutableCycles, ComprehensionsAndLambdasLeaveNoCycle)
{
    protoPython::PythonEnvironment env(STDLIB_PATH);
    proto::ProtoContext* ctx = env.getContext();
    ASSERT_NE(ctx, nullptr);
    proto::ProtoSpace* space = env.getSpace();
    ASSERT_NE(space, nullptr);

    auto inCycles = [&]() {
        const proto::MutableGraphReport r = space->findMutableCycles(ctx);
        EXPECT_FALSE(r.truncated) << "the scan ran out of cell budget";
        return handlesInCycles(r);
    };

    // Define first, so that the measured run adds no function to the module.
    ASSERT_EQ(env.executeString(
                  "def _mc_run():\n"
                  "    total = 0\n"
                  "    for i in range(40):\n"
                  "        total += sum([i + j for j in range(3)])\n"
                  "        total += sum(i + j for j in range(3))\n"
                  "        total += (lambda: i)()\n"
                  "    return total\n"
                  "def _mc_recursive():\n"
                  "    def factory(k):\n"
                  "        def rec(n):\n"
                  "            return k if n == 0 else rec(n - 1)\n"
                  "        return rec\n"
                  "    return [factory(k)(3) for k in range(5)]\n",
                  "<mutable-cycles:define>"),
              0);
    const proto::proto_ulong base = inCycles();
    ASSERT_EQ(env.executeString("assert _mc_run() == 2 * 2460 + 780\n",
                                "<mutable-cycles:incidental>"),
              0);
    EXPECT_EQ(inCycles(), base)
        << "comprehensions, generator expressions or lambdas left mutable handles "
           "in a cycle: their closure frames still bind their own names";

    // A recursive nested function still finds itself.
    ASSERT_EQ(env.executeString("assert _mc_recursive() == [0, 1, 2, 3, 4]\n",
                                "<mutable-cycles:recursive>"),
              0);
}

// A named nested def stored as a local of the call that defines it, and the
// closures and classes defined in such calls, leave no cycle.  With closures
// that held their defining frame, every call of a function defining a named
// nested function left one cycle between mutable objects (the frame held the
// function as a local, the function held the frame), which protoCore never
// collects: a loop of such calls exhausted any fixed heap.  Closures now hold
// cells; a recursive nested def binds its own name to itself on entry
// instead of closing over the cell that holds it (Compiler.h, "Closures").
//
// Not covered, because they are cycles by what the program means and stay
// (docs/INTERNALS_DEEP_DIVE.md, "Closures"): mutually recursive nested
// functions, a decorated recursive nested function, and a class defined in a
// call (a class's __mro__ holds the class).
TEST(MutableCycles, NamedNestedDefsLeaveNoCycle)
{
    protoPython::PythonEnvironment env(STDLIB_PATH);
    proto::ProtoContext* ctx = env.getContext();
    ASSERT_NE(ctx, nullptr);
    proto::ProtoSpace* space = env.getSpace();
    ASSERT_NE(space, nullptr);

    auto inCycles = [&]() {
        const proto::MutableGraphReport r = space->findMutableCycles(ctx);
        EXPECT_FALSE(r.truncated) << "the scan ran out of cell budget";
        return handlesInCycles(r);
    };

    ASSERT_EQ(env.executeString(
                  "def _nd_plain(k):\n"
                  "    def get():\n"
                  "        return k\n"
                  "    return get()\n"
                  "def _nd_recursive(k):\n"
                  "    def rec(n):\n"
                  "        return k if n == 0 else rec(n - 1)\n"
                  "    return rec(3)\n"
                  "def _nd_recursive_comprehension(k):\n"
                  "    def depth(t):\n"
                  "        return 1 + max([depth(c) for c in t], default=0)\n"
                  "    return depth([[[]]]) + k\n"
                  "def _nd_recursive_generator(k):\n"
                  "    def walk(n):\n"
                  "        if n:\n"
                  "            yield n\n"
                  "            yield from walk(n - 1)\n"
                  "    return sum(walk(3)) + k\n"
                  "def _nd_nonlocal(k):\n"
                  "    total = 0\n"
                  "    def add(v):\n"
                  "        nonlocal total\n"
                  "        total += v\n"
                  "    add(k)\n"
                  "    return total\n"
                  "def _nd_returned(k):\n"
                  "    x = [k]\n"
                  "    return lambda: x[0]\n"
                  "def _nd_run(n):\n"
                  "    t = 0\n"
                  "    for i in range(n):\n"
                  "        t += _nd_plain(i) + _nd_recursive(i) + _nd_recursive_comprehension(i)\n"
                  "        t += _nd_recursive_generator(i) + _nd_nonlocal(i) + _nd_returned(i)()\n"
                  "    return t\n",
                  "<mutable-cycles:nested-def-define>"),
              0);
    const proto::proto_ulong base = inCycles();
    ASSERT_EQ(env.executeString("assert _nd_run(40) == sum(6 * i + 9 for i in range(40))\n",
                                "<mutable-cycles:nested-def-run>"),
              0);
    EXPECT_EQ(inCycles(), base)
        << "calls defining named nested functions left mutable handles in a cycle";
}

// A function whose locals a nested scope captures keeps them on its (mutable)
// frame, and records that frame as its namespace for zero-argument super().
// It used to record it as the frame ITSELF, a one-handle cycle, so every
// CALL of such a function -- `def scale(xs, k): return [x * k for x in xs]`
// is one -- kept its frame for the life of the space: 300 calls, 300 cycles,
// and a loop of 60,000 calls drove the live set to a 3 M-cell ceiling.
TEST(MutableCycles, AMappedFrameDoesNotReferenceItself)
{
    protoPython::PythonEnvironment env(STDLIB_PATH);
    proto::ProtoContext* ctx = env.getContext();
    ASSERT_NE(ctx, nullptr);
    proto::ProtoSpace* space = env.getSpace();
    ASSERT_NE(space, nullptr);

    auto namespaceCycles = [&]() {
        const proto::MutableGraphReport r = space->findMutableCycles(ctx);
        EXPECT_FALSE(r.truncated) << "the scan ran out of cell budget";
        return countShape(r, ".__frame_namespace__");
    };
    const proto::proto_ulong base = namespaceCycles();

    // The zero-argument super() inside a method defined in a mapped function
    // is what reads the namespace: it must still find __class__ there.
    ASSERT_EQ(env.executeString(
                  "def _mf_scale(xs, k):\n"
                  "    return [x * k for x in xs]\n"
                  "def _mf_run():\n"
                  "    total = 0\n"
                  "    for i in range(30):\n"
                  "        total += sum(_mf_scale([1, 2, 3], i))\n"
                  "    return total\n"
                  "def _mf_classes(tag):\n"
                  "    class Base:\n"
                  "        def who(self):\n"
                  "            return 'base'\n"
                  "    class Derived(Base):\n"
                  "        def who(self):\n"
                  "            return tag + super().who()\n"
                  "    return Derived().who()\n"
                  "assert _mf_run() == 6 * 435\n"
                  "assert _mf_classes('d-') == 'd-base'\n",
                  "<mutable-cycles:mapped-frame>"),
              0);
    EXPECT_EQ(namespaceCycles(), base)
        << "a mapped frame still references itself as its namespace: every call "
           "of a function whose locals a nested scope captures is kept for ever";
}

// Creating a function must not add a permanent GC root.  createUserFunction
// pinned each function's metadata cache (a byte buffer, which references no
// cell) in the environment's root set for the life of the environment, and
// the function already keeps the buffer through its own attribute.  So every
// comprehension, lambda or nested def EXECUTED added one root for ever: the
// set grew without bound and the collector scans it inside every
// stop-the-world pause.
TEST(MutableCycles, CreatingAFunctionAddsNoPermanentRoot)
{
    protoPython::PythonEnvironment env(STDLIB_PATH);
    proto::ProtoRootSet* roots = env.getGcRoots();
    ASSERT_NE(roots, nullptr);
    // Warm up: the first call may compile, intern and pin what is pinned once.
    ASSERT_EQ(env.executeString("def _fr_make(k):\n"
                                "    return lambda: k\n"
                                "_fr_once = [_fr_make(0)() for _ in range(2)]\n",
                                "<roots:warm>"),
              0);
    const proto::proto_ulong before = roots->size();
    ASSERT_EQ(env.executeString("_fr_sum = sum(_fr_make(k)() for k in range(200))\n"
                                "assert _fr_sum == 19900\n",
                                "<roots:run>"),
              0);
    EXPECT_LE(roots->size(), before + 2)
        << "creating 200 functions added " << (roots->size() - before)
        << " permanent roots";
}

// Zero-argument and explicit super() calls, run many times, leave nothing in a
// cycle: each call builds a proxy, and a proxy whose own cells were bound to
// it was kept by the mutables table for the life of the space.  Measured on
// handles inside cycles, which cannot fall when components merge.
TEST(MutableCycles, SuperCallsLeaveNoCycle)
{
    protoPython::PythonEnvironment env(STDLIB_PATH);
    proto::ProtoContext* ctx = env.getContext();
    ASSERT_NE(ctx, nullptr);
    proto::ProtoSpace* space = env.getSpace();
    ASSERT_NE(space, nullptr);

    auto inCycles = [&]() {
        const proto::MutableGraphReport r = space->findMutableCycles(ctx);
        EXPECT_FALSE(r.truncated) << "the scan ran out of cell budget";
        return handlesInCycles(r);
    };

    ASSERT_EQ(env.executeString(
                  "class _SpBase:\n"
                  "    def __init__(self):\n"
                  "        self.v = 1\n"
                  "    def m(self):\n"
                  "        return self.v\n"
                  "class _SpDerived(_SpBase):\n"
                  "    def __init__(self):\n"
                  "        super().__init__()\n"
                  "    def m(self):\n"
                  "        return super().m() + super(_SpDerived, self).m()\n"
                  "def _sp_run(n):\n"
                  "    t = 0\n"
                  "    for _ in range(n):\n"
                  "        t += _SpDerived().m()\n"
                  "    return t\n"
                  "assert _sp_run(2) == 4\n",
                  "<mutable-cycles:super-define>"),
              0);
    const proto::proto_ulong base = inCycles();
    ASSERT_EQ(env.executeString("assert _sp_run(50) == 100\n",
                                "<mutable-cycles:super-run>"),
              0);
    EXPECT_EQ(inCycles(), base)
        << "150 super() calls left mutable handles in a cycle";
}
