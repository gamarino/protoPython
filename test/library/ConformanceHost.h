// protoPython's conformance Host.
//
// Every capability runs through protoPython's own interpreter
// (PythonEnvironment::executeString), never through direct protoCore calls: the
// suite exists to audit what protoPython does, and a Host that reaches past its
// runtime audits protoCore instead.
//
// See docs/CONFORMANCE.md for the judgement answers (C3, C5, C7).
#pragma once

#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <protoCoreConformance.h>

#include <string>

namespace protoPython {
namespace conformance {

class PythonConformanceHost final : public proto::conformance::Host {
public:
    PythonConformanceHost() : env_(stdlibPath()) {}

    const char* name() const override { return "protoPython"; }

    proto::ProtoContext* mainContext() override { return env_.getContext(); }

    // --- rule 1 ------------------------------------------------------------
    //
    // A Python loop that builds and drops a string per iteration.  The shape is
    // `test/regression/re_results_not_interned.py`, which exists to bound RSS
    // growth over exactly this kind of loop.
    //
    // The return value is a DECLARATION -- rounds completed -- not the
    // denominator.  The denominator is the space's own in-use cell count,
    // measured by the case.  A delta of ProtoContext::allocatedCellsCount would
    // be actively misleading: protoPython's dispatch loop calls safepoint()
    // every 64 opcodes, and each submission zeroes that counter, so the delta is
    // smallest exactly when protoPython is conforming.
    unsigned long makeGarbage(unsigned long requestedCells) override {
        return runUntilConsumed(requestedCells,
            "def _conf_churn(n):\n"
            "    total = 0\n"
            "    for i in range(n):\n"
            "        s = 'garbage-%d-%d' % (i, i * 7)\n"
            "        total += len(s)\n"
            "    return total\n"
            "_conf_sink = _conf_churn(20000)\n");
    }

    // --- rule 5 ------------------------------------------------------------
    //
    // C5: measured on the TUPLE, deliberately, and this is the one choice in the
    // adaptor that decides an answer.  protoPython has two user-visible
    // sequences, list and tuple.  A Python tuple is a first-class sequence a
    // user gets from a literal, and protoPython builds it out of protoCore's
    // ProtoTuple, whose nodes are interned and perennial (OP_BUILD_TUPLE,
    // OP_LIST_TO_TUPLE, and the *args binding each allocate one per execution).
    // Measuring the list instead would make this case pass, and would be
    // choosing the workload to get the answer -- the same defect as a fixture
    // written so that it cannot go red.
    unsigned long makeSequenceGarbage(unsigned long requestedCells) override {
        return runUntilConsumed(requestedCells,
            "def _conf_tchurn(n):\n"
            "    total = 0\n"
            "    for i in range(n):\n"
            "        t = (i, 'payload-%d' % i, (i, i + 1))\n"
            "        total += len(t)\n"
            "    return total\n"
            "_conf_tsink = _conf_tchurn(20000)\n");
    }

    // --- rule 4 ------------------------------------------------------------
    //
    // protoPython's own attribute-key interning: getInternedString, which the
    // header documents as "for vocabulary only".
    const proto::ProtoObject* internAttributeKey(const char* text) override {
        return reinterpret_cast<const proto::ProtoObject*>(
            PythonEnvironment::getInternedString(mainContext(), std::string(text)));
    }

    // --- rules 3 and 8 -----------------------------------------------------
    //
    // protoPython's own multi-producer / multi-consumer path, in Python: the
    // shape of `test/regression/deque_threads.py`, which exists to prove that
    // deque mutators called from parallel threads keep every item exactly once.
    // The backlog is bounded by draining in the same round, so the live set
    // stays small and a failure means the collector could not keep up rather
    // than that the workload was unboundedly live.
    bool runProducerConsumer(unsigned long units) override {
        // BOUNDED BACKLOG, and that is load-bearing.  The first draft started
        // four producers, joined them all, and only then drained -- so 200,000
        // items were live at once by construction, the live set legitimately
        // exceeded any ceiling, and rule 8 "failed" with an out-of-memory abort
        // that said nothing about protoPython.  Here each round produces and
        // drains 2,000 items, so at most 2,000 are ever in flight and a failure
        // means the collector could not keep up.
        const unsigned long perRound = 2000;
        const unsigned long rounds = (units / perRound) + 1;
        const std::string src =
            "import threading\n"
            "from collections import deque\n"
            "def _conf_pc(rounds, per_round):\n"
            "    total = 0\n"
            "    for r in range(rounds):\n"
            "        d = deque()\n"
            "        def producer(base):\n"
            "            for i in range(per_round // 2):\n"
            "                d.append(base + i)\n"
            "        ts = [threading.Thread(target=producer, args=(t * per_round,))\n"
            "              for t in range(2)]\n"
            "        for t in ts:\n"
            "            t.start()\n"
            "        for t in ts:\n"
            "            t.join()\n"
            "        while True:\n"
            "            try:\n"
            "                d.popleft()\n"
            "            except IndexError:\n"
            "                break\n"
            "            total += 1\n"
            "    return total\n"
            "_conf_pc_result = _conf_pc(" + std::to_string(rounds) + ", "
                + std::to_string(perRound) + ")\n";
        if (env_.executeString(src, "<conformance:producer_consumer>") != 0) {
            env_.takePendingException();
            return false;
        }
        return readIntGlobal("_conf_pc_result") ==
               (long long) (rounds * (perRound / 2) * 2);
    }

    // --- rule 2b -----------------------------------------------------------
    //
    // protoPython's own thread and its own join: threading.Thread goes through
    // _thread.start_joinable_thread, which is ProtoSpace::newThread
    // (ThreadModule.cpp), and Thread.join() reaches ProtoThread::join with an
    // UnmanagedScope around it.  The worker allocates, so it reaches a safepoint
    // every 64 opcodes and cannot itself hold the quorum -- which matters, or
    // the case would be measuring the wrong thread.
    //
    // The release flag is not polled: Python cannot read a C++ flag, and adding
    // a builtin for the suite's benefit would make the adaptor test something
    // protoPython does not do.  The worker instead does a few seconds of its own
    // allocating work and finishes, which is what the case needs.
    bool joinBlockingThread(volatile bool* /*releaseFlag*/) override {
        const std::string src =
            "import threading\n"
            "def _conf_worker():\n"
            "    total = 0\n"
            "    for i in range(400000):\n"
            "        total += len('spin-%d' % i)\n"
            "    return total\n"
            "_conf_t = threading.Thread(target=_conf_worker)\n"
            "_conf_t.start()\n"
            "_conf_t.join()\n"
            "_conf_joined = 1\n";
        if (env_.executeString(src, "<conformance:join>") != 0) {
            env_.takePendingException();
            return false;
        }
        return readIntGlobal("_conf_joined") == 1;
    }

    // --- rules 2 and 11 ----------------------------------------------------
    //
    // forEachThreadKind is NOT implemented, so the case reports NotApplicable
    // rather than a pass.  protoPython creates exactly two kinds of OS thread
    // beyond the main one, both through ProtoSpace::newThread
    // (ThreadModule.cpp), and offers no hook to run arbitrary C++ on a live
    // Python thread.  Spawning one here through newThread ourselves would audit
    // protoCore's registration rather than protoPython's, and would report a
    // pass for a property nobody checked.  Recorded in docs/CONFORMANCE.md.

    // C7: protoPython has 15 fromExternalPointer sites and keeps NO byte total
    // anywhere, so the default (-1) is the honest answer and the case reports
    // NeedsReview.  That is a real MemoryModel.md section 5 gap, not a
    // formality: a program holding a million deques has a million untracked C++
    // allocations, invisible to heapSize and therefore to the collection
    // trigger.

private:
    static std::string stdlibPath() {
#ifdef STDLIB_PATH
        return STDLIB_PATH;
#else
        return "";
#endif
    }

    long long readIntGlobal(const char* name) {
        proto::ProtoContext* ctx = env_.getContext();
        const proto::ProtoObject* main = env_.resolve("__main__", ctx);
        if (!main || main == PROTO_NONE) return -1;
        const proto::ProtoObject* v = env_.getAttr(main, name);
        if (!v || v == PROTO_NONE) return -1;
        return v->asLong(ctx);
    }

    unsigned long runUntilConsumed(unsigned long requestedCells,
                                   const char* snippet) {
        const long floor = inUse() + (long) (requestedCells + requestedCells / 2);
        unsigned long rounds = 0;
        while (inUse() < floor && rounds < 4096) {
            if (env_.executeString(snippet, "<conformance:garbage>") != 0) {
                env_.takePendingException();
                return rounds;
            }
            ++rounds;
        }
        return rounds;
    }

    long inUse() {
        proto::ProtoSpace* s = env_.getSpace();
        return (long) s->heapSize - (long) s->freeCellsCount;
    }

    PythonEnvironment env_;
};

}  // namespace conformance
}  // namespace protoPython
