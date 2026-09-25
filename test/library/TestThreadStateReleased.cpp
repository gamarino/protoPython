// A finished thread, and a dropped function, must leave nothing reachable.
//
// These two properties were both violated, both silently, and both showed up as
// "protoPython retains a few hundred marked cells per finished thread" in
// protoCore's rule-8 embedder-conformance case (docs/CONFORMANCE.md):
//
//   1. Every OS thread's `py_thread` record was pinned by storing it as an
//      attribute of one process-global mutable object, keyed by thread id.
//      Nothing removed the entry, and `setAttribute` STRONG-interns its key, so
//      each finished thread also left a permanent Symbol behind.  It is now a
//      `ProtoRootSet` handle released by `PythonEnvironment::releaseThreadState`,
//      and `getPyThreadRoots()->size()` makes that checkable.
//
//   2. Every function object created at run time held the frame it was created
//      in (`__closure_frames__`), while that frame bound the function under its
//      own name.  Two MUTABLE protoCore objects in a reference cycle can never be
//      reclaimed -- a mutable's live state hangs off `ProtoSpace::mutableRoot`,
//      mark treats that table as a root, and the entry is released only once the
//      handle cell is finalized -- so every closure and every lambda was
//      immortal, at ~62 marked cells each.  `createUserFunction` now installs the
//      reference only when the function's own bytecode could use it.
//
// Both assertions are measurements with headroom, not exact figures: the
// collector legitimately lags (a young chain submitted after the dirtySegments
// exchange belongs to the next cycle), so a small residual is expected and a
// per-object residual in the hundreds is the defect.
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

using namespace protoPython;

namespace {

/// Request one collection cycle and wait, bounded, for it to finish.
///
/// Two details are load-bearing and both are copied from protoCore's own
/// CycleDriver.  `triggerGC` is advisory -- it does nothing unless free cells are
/// under 20% -- so a cycle must be demanded by setting `gcStarted` directly.  And
/// the wait happens inside an `UnmanagedScope`, NOT in a `safepoint()` poll loop:
/// both meet the stop-the-world quorum, but `safepoint()` would also submit this
/// context's own young generation, which is exactly the garbage the measurement
/// is trying to attribute.
bool requestOneCycle(proto::ProtoSpace& space, proto::ProtoContext* ctx,
                     unsigned deadlineMs)
{
    const auto lockDeadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(deadlineMs);
    bool requested = false;
    while (std::chrono::steady_clock::now() < lockDeadline) {
        if (proto::ProtoSpace::globalMutex.try_lock()) {
            space.gcStarted = true;
            space.gcCV.notify_all();
            proto::ProtoSpace::globalMutex.unlock();
            requested = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!requested) return false;
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(deadlineMs);
    {
        proto::ProtoContext::UnmanagedScope parked(ctx);
        while (space.gcStarted.load()
               && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (space.gcStarted.load()) return false;
    // gcStarted is cleared before sweep finishes, so give the sweep room before
    // the next sample or the reclaimed figure is not yet final.
    {
        proto::ProtoContext::UnmanagedScope parked(ctx);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    return true;
}

/// Cycle until the in-use figure stops falling: two consecutive flat cycles,
/// because a young chain submitted after the dirtySegments exchange belongs to
/// the NEXT cycle, so one flat cycle is not yet evidence.
void driveCycles(proto::ProtoSpace& space, proto::ProtoContext* ctx,
                 unsigned maxCycles, unsigned deadlineMs)
{
    long previousInUse = (long) space.heapSize - (long) space.freeCellsCount;
    unsigned stable = 0;
    for (unsigned i = 0; i < maxCycles; ++i) {
        if (!requestOneCycle(space, ctx, deadlineMs)) return;
        const long inUse = (long) space.heapSize - (long) space.freeCellsCount;
        if (inUse >= previousInUse) { if (++stable >= 2) return; }
        else stable = 0;
        previousInUse = inUse;
    }
}

long markedCells(proto::ProtoSpace& space)
{
    return (long) space.liveCellsLastCycle.load(std::memory_order_relaxed);
}

/// ONE environment for the whole process.
///
/// protoPython is one runtime per process -- `PythonEnvironment` shares a
/// singleton `ProtoSpace` (`getProcessSpace`) and a good deal of `thread_local`
/// state with it.  A second instance in the same process comes up without a
/// working import system ("No module named 'os'"), so every test here shares
/// this one.  Each test's measurement is a DELTA taken after settling, so
/// sharing costs nothing: what an earlier test left behind is part of the
/// baseline, not of the figure.
PythonEnvironment& sharedEnv()
{
    static PythonEnvironment env(STDLIB_PATH);
    return env;
}

class RetentionTest : public ::testing::Test {
protected:
    RetentionTest() : env_(sharedEnv()) {}

    void settle() { driveCycles(space(), ctx(), 6, 15000); }
    proto::ProtoContext* ctx() { return env_.getContext(); }
    proto::ProtoSpace& space() { return *env_.getContext()->space; }

    /// Marked-cell growth caused by running `call`, with the definitions in
    /// `defs` already compiled and settled so that the one-off cost of compiling
    /// (protoPython pins each compiled module's bytecode for the life of the
    /// environment) is not counted as retention.
    long retentionOf(const std::string& defs, const std::string& call) {
        if (env_.executeString(defs, "<retention:defs>") != 0) {
            env_.takePendingException();
            ADD_FAILURE() << "definitions failed to execute";
            return -1;
        }
        settle();
        const long before = markedCells(space());
        if (env_.executeString(call, "<retention:call>") != 0) {
            env_.takePendingException();
            ADD_FAILURE() << "workload failed to execute";
            return -1;
        }
        driveCycles(space(), ctx(), 8, 15000);
        return markedCells(space()) - before;
    }

    PythonEnvironment& env_;
};

// --- property 1: a finished thread releases its py_thread pin ---------------
TEST_F(RetentionTest, FinishedThreadReleasesItsPyThreadRoot)
{
    proto::ProtoRootSet* roots = env_.getPyThreadRoots();
    ASSERT_NE(roots, nullptr) << "the py_thread root set must exist";

    const std::string src =
        "import threading\n"
        "def _noop():\n"
        "    return 1\n"
        "def _run(n):\n"
        "    ts = [threading.Thread(target=_noop) for _ in range(n)]\n"
        "    for t in ts:\n"
        "        t.start()\n"
        "    for t in ts:\n"
        "        t.join()\n"
        "    return n\n";
    ASSERT_EQ(env_.executeString(src, "<retention:threads>"), 0);
    ASSERT_EQ(env_.executeString("_r = _run(2)\n", "<retention:warm>"), 0);

    // Only the main thread's record may still be pinned once every spawned
    // thread has been joined.  Whatever that baseline is, 24 more threads must
    // not change it.
    const unsigned long baseline = roots->size();
    ASSERT_EQ(env_.executeString("_r = _run(24)\n", "<retention:many>"), 0);
    EXPECT_EQ(roots->size(), baseline)
        << "24 threads started and joined left " << (roots->size() - baseline)
        << " py_thread pins behind; releaseThreadState did not run, or did not "
           "reach the root set (it must be called while the thread's context is "
           "still registered).";
}

// --- property 2: a dropped function object is reclaimable -------------------
TEST_F(RetentionTest, DroppedFunctionObjectsAreReclaimed)
{
    // A SLOPE over the number of function objects, with the round count held
    // fixed.  That is the only honest shape: the per-round cost (the list
    // comprehension is itself a function object, and it DOES capture free
    // variables, so it legitimately keeps its frame) and the one-off cost of
    // compiling the snippet are identical in both runs and cancel out.
    const std::string defs =
        "def _mkmany(rounds, per_round):\n"
        "    for r in range(rounds):\n"
        "        xs = [(lambda: None) for _ in range(per_round)]\n"
        "        del xs\n"
        "    return rounds * per_round\n";
    const long small = retentionOf(defs, "_r = _mkmany(20, 10)\n");   //  200 fns
    ASSERT_GE(small, 0);
    const long large = retentionOf(defs, "_r = _mkmany(20, 60)\n");   // 1200 fns
    ASSERT_GE(large, 0);

    const double perFunction = (double) (large - small) / 1000.0;
    EXPECT_LT(perFunction, 20.0)
        << "each dropped function object left " << perFunction
        << " marked cells behind (" << small << " cells for 200 functions, "
        << large << " for 1200).  Before the fix this slope was ~62 cells and "
           "flat across 30 further collection cycles: a function object that "
           "holds the frame it was created in, while that frame binds the "
           "function under its own name, is a reference cycle between two "
           "MUTABLE protoCore objects and can never be reclaimed.  See "
           "createUserFunction's needsClosureFrame.";
}

// --- property 3: a finished thread is bounded, not linear ------------------
TEST_F(RetentionTest, FinishedThreadsDoNotAccumulateMarkedCells)
{
    // The figure rule 8 was blamed on, measured as a slope over the THREAD count
    // with the round count fixed, so that nothing but threads varies.
    const std::string defs =
        "import threading\n"
        "def _t_noop():\n"
        "    return 1\n"
        "def _t_run(rounds, n):\n"
        "    for r in range(rounds):\n"
        "        ts = [threading.Thread(target=_t_noop) for _ in range(n)]\n"
        "        for t in ts:\n"
        "            t.start()\n"
        "        for t in ts:\n"
        "            t.join()\n"
        "        del ts\n"
        "    return rounds * n\n";
    const long small = retentionOf(defs, "_r = _t_run(10, 2)\n");   // 20 threads
    ASSERT_GE(small, 0);
    const long large = retentionOf(defs, "_r = _t_run(10, 10)\n");  // 100 threads
    ASSERT_GE(large, 0);

    const double perThread = (double) (large - small) / 80.0;
    EXPECT_LT(perThread, 120.0)
        << "each finished thread left " << perThread << " marked cells behind ("
        << small << " cells for 20 threads, " << large << " for 100).  Before the "
           "fixes this slope was ~300 cells.  The contributors were the "
           "process-global thread-roots dict, threading._dangling (protoPython's "
           "WeakSet is a STRONG registry, so it needs an explicit evict), and one "
           "per-Thread _make_invoke_excepthook closure caught in the mutable "
           "function<->frame cycle.";
}

}  // namespace
