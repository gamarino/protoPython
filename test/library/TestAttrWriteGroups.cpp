// Attribute write groups, measured: allocation per construction, and what a
// reader on another thread can observe.
//
// `self.a = x; self.b = y; ...` on an ordinary instance publishes one new
// version of the object per write (a new snapshot plus a path copy in
// protoCore's mutable table).  Compiler::tryCompileAttrGroup compiles such a
// run so that the engine publishes it once (ProtoObject::setAttributes).  The
// semantic cases live in test/regression/attr_write_groups.py; this file
// measures what the grouping is for, comparing code compiled with the groups
// on and off (Compiler::setAttrWriteGroups) in one process.
//
// Its own executable: the allocation figures read the shared space's heap, so
// another test's work in the same process would show up in them.

#include <protoPython/Compiler.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

#if defined(_WIN32)
void setEnvVar(const char* name, const char* value) { _putenv_s(name, value); }
#else
void setEnvVar(const char* name, const char* value) { setenv(name, value, 1); }
#endif

// One environment per process (it owns the process space).  A heap ceiling
// high enough that no collection runs during a measurement is set before the
// space is created.
protoPython::PythonEnvironment& sharedEnv() {
    static protoPython::PythonEnvironment* env = [] {
        setEnvVar("PROTOCORE_HEAP_LIMIT_CELLS", "200000000");
        return new protoPython::PythonEnvironment(STDLIB_PATH);
    }();
    return *env;
}

// Compiles and runs `src` with the groups on or off.
void run(const std::string& src, bool groups, const char* name) {
    protoPython::Compiler::setAttrWriteGroups(groups);
    const int rc = sharedEnv().executeString(src, name);
    protoPython::Compiler::setAttrWriteGroups(true);
    ASSERT_EQ(rc, 0) << name << " failed to execute";
}

long long heapInUse(proto::ProtoSpace* space) {
    return static_cast<long long>(space->heapSize) - space->freeCellsCount;
}

}  // namespace

// A constructor that stores five attributes: per write, five publications;
// grouped, one.
TEST(AttrWriteGroups, AGroupedConstructionAllocatesFewerCells) {
    proto::ProtoSpace* space = sharedEnv().getContext()->space;
    const char* defs =
        "class P5_%s:\n"
        "    def __init__(self, i):\n"
        "        self.id = i\n"
        "        self.name = 'n'\n"
        "        self.qty = i\n"
        "        self.price = 2\n"
        "        self.flag = True\n"
        "def build_%s(n):\n"
        "    keep = None\n"
        "    for i in range(n):\n"
        "        keep = P5_%s(i)\n"
        "    return keep\n"
        "build_%s(1000)\n";
    const int N = 50000;
    double cells[2] = {0, 0};
    for (int round = 0; round < 2; ++round) {
        for (int g = 0; g < 2; ++g) {
            const char* tag = g ? "on" : "off";
            char buf[1024];
            std::snprintf(buf, sizeof buf, defs, tag, tag, tag, tag);
            if (round == 0) run(buf, g == 1, "<defs>");
            const std::string call = std::string("build_") + tag + "(" + std::to_string(N) + ")\n";
            const uint64_t cycles = space->getGCCycleCount();
            const long long before = heapInUse(space);
            run(call, g == 1, "<build>");
            const long long after = heapInUse(space);
            if (space->getGCCycleCount() != cycles)
                GTEST_SKIP() << "a collection ran during the measurement";
            const double perOp = double(after - before) / N;
            std::printf("[ CELLS    ] round %d, groups %s: %.1f cells per construction\n",
                        round, tag, perOp);
            cells[g] = (round == 0) ? perOp : std::min(cells[g], perOp);
        }
    }
    std::fflush(stdout);
    // Four of the five publications are spared, about ten cells each.
    EXPECT_LT(cells[1], cells[0] - 20.0)
        << "per write " << cells[0] << " cells, grouped " << cells[1];
}

namespace {

constexpr int kRounds = 100000;
const proto::ProtoObject* gShared = nullptr;
const proto::ProtoString* gNames[3] = {nullptr, nullptr, nullptr};
std::atomic<bool> gWriterDone{false};
std::atomic<long> gReads{0};
std::atomic<long> gTorn{0};
std::atomic<long> gMidway{0};

// Takes one snapshot of the shared object's own attributes at a time and
// checks that its three attributes carry the same round number.
const proto::ProtoObject* snapshotReader(proto::ProtoContext* c, const proto::ProtoObject*,
                                         const proto::ParentLink*, const proto::ProtoList*,
                                         const proto::ProtoSparseList*) {
    while (!gWriterDone.load(std::memory_order_acquire)) {
        const proto::ProtoSparseList* snap = gShared->getOwnAttributes(c);
        const proto::ProtoObject* v[3];
        for (int i = 0; i < 3; ++i)
            v[i] = snap->getAt(c, reinterpret_cast<uintptr_t>(gNames[i]));
        if (v[0] != v[1] || v[1] != v[2]) {
            gTorn.fetch_add(1, std::memory_order_relaxed);
        } else if (v[0] && proto::isSmallInt(v[0])) {
            const long long k = proto::asSmallInt(v[0]);
            if (k > 0 && k < kRounds) gMidway.fetch_add(1, std::memory_order_relaxed);
        }
        gReads.fetch_add(1, std::memory_order_relaxed);
    }
    return PROTO_NONE;
}

// Runs the writer loop on this thread while a protoCore thread takes
// snapshots of the object; answers the number of torn snapshots.
long tornSnapshots(bool groups) {
    protoPython::PythonEnvironment& env = sharedEnv();
    proto::ProtoContext* ctx = env.getContext();
    const std::string tag = groups ? "on" : "off";
    const std::string defs =
        "class H_" + tag + ":\n"
        "    def __init__(self):\n"
        "        self.a = 0\n"
        "        self.b = 0\n"
        "        self.c = 0\n"
        "    def set(self, k):\n"
        "        self.a = k\n"
        "        self.b = k\n"
        "        self.c = k\n"
        "import builtins\n"
        "builtins.shared_" + tag + " = H_" + tag + "()\n"
        "def write_" + tag + "(n):\n"
        "    o = builtins.shared_" + tag + "\n"
        "    for k in range(1, n + 1):\n"
        "        o.set(k)\n"
        "    return o.a\n";
    run(defs, groups, "<writer-defs>");
    const proto::ProtoObject* h = env.resolve("shared_" + tag);
    EXPECT_NE(h, nullptr);
    if (!h) return -1;
    gShared = h;
    gNames[0] = proto::ProtoString::createSymbol(ctx, "a");
    gNames[1] = proto::ProtoString::createSymbol(ctx, "b");
    gNames[2] = proto::ProtoString::createSymbol(ctx, "c");
    gWriterDone = false;
    gReads = 0;
    gTorn = 0;
    gMidway = 0;
    const proto::ProtoThread* reader = ctx->space->newThread(
        ctx, proto::ProtoString::createSymbol(ctx, "attr-group-reader"), snapshotReader,
        nullptr, nullptr);
    run("import builtins\nbuiltins._last_" + tag + " = write_" + tag + "("
            + std::to_string(kRounds) + ")\n",
        groups, "<writer>");
    gWriterDone = true;
    const_cast<proto::ProtoThread*>(reader)->join(ctx);
    const proto::ProtoObject* last = env.resolve("_last_" + tag);
    EXPECT_TRUE(last && proto::isSmallInt(last) && proto::asSmallInt(last) == kRounds);
    gShared = nullptr;
    return gTorn.load();
}

}  // namespace

// Another thread sees all of a group or none of it.
TEST(AttrWriteGroups, AnotherThreadSeesAllOfAGroupOrNone) {
    const long perWriteTorn = tornSnapshots(false);
    std::printf("[ TORN     ] per write: %ld torn snapshots of %ld (%ld mid-run)\n",
                perWriteTorn, gReads.load(), gMidway.load());
    const long groupedTorn = tornSnapshots(true);
    std::printf("[ TORN     ] grouped: %ld torn snapshots of %ld (%ld mid-run)\n",
                groupedTorn, gReads.load(), gMidway.load());
    std::fflush(stdout);
    EXPECT_EQ(groupedTorn, 0) << "a reader saw part of a group";
    // The reader must have run while the writer did, or nothing was shown.
    EXPECT_GT(gMidway.load(), 0);
}
