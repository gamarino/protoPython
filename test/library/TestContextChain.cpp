// Every ProtoContext protoPython creates must be chained onto the thread's
// CURRENT context.
//
// protoCore finds a thread's roots by walking from its current context through
// `previous`, and ~ProtoContext makes `previous` current again.  A context
// created with a `previous` other than the current one hides every context in
// between from each collection while it lives, and when it is destroyed it
// leaves an OUTER context current while inner frames are still executing --
// those frames, their operand stacks, locals and young objects, are then
// invisible to the collector until they return.  Under a heap limit that freed
// live objects (crashes and corrupted module-level strings on Windows, where
// test/regression/file_close_on_collect.py reproduced it in about one run in
// ten).
//
// The defect is checked here deterministically, without waiting for a
// collection to land in the window: a native `_chain_ok()` builtin reports
// whether the context it is called with -- the calling Python frame's -- is on
// the chain the collector walks.  On the defective build it is not, after any
// native call that re-entered Python through a context it was handed (the
// interpreter's own s_threadContext, a generator's resumer, exec, import).
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <gtest/gtest.h>

#include <sstream>
#include <string>

using namespace protoPython;

namespace {

proto::ProtoContext* threadCurrent(proto::ProtoContext* ctx) {
    if (ctx->thread) return ctx->thread->getCurrentContext();
    return ctx->space ? ctx->space->mainContext : nullptr;
}

bool onCurrentChain(proto::ProtoContext* ctx) {
    for (proto::ProtoContext* c = threadCurrent(ctx); c; c = c->previous)
        if (c == ctx) return true;
    return false;
}

// _chain_ok(): True when the calling frame's context is reachable from the
// thread's current context.
const proto::ProtoObject* chainOk(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                  const proto::ParentLink*, const proto::ProtoList*,
                                  const proto::ProtoSparseList*) {
    return onCurrentChain(ctx) ? PROTO_TRUE : PROTO_FALSE;
}

class ContextChain : public ::testing::Test {
protected:
    static PythonEnvironment& env() {
        static PythonEnvironment instance(STDLIB_PATH);
        static bool installed = false;
        if (!installed) {
            proto::ProtoContext* ctx = instance.getContext();
            const proto::ProtoObject* builtins = instance.getBuiltins();
            const_cast<proto::ProtoObject*>(builtins)->setAttribute(
                ctx, PythonEnvironment::getInternedString(ctx, "_chain_ok"),
                ctx->fromMethod(nullptr, chainOk));
            installed = true;
        }
        return instance;
    }

    // Runs `src`, which asserts with _chain_ok() itself; returns the
    // traceback of an uncaught exception, or "" on success.
    static std::string run(const std::string& src, const char* name) {
        if (env().executeString(src, name) == 0) return "";
        std::ostringstream out;
        const proto::ProtoObject* exc = env().takePendingException();
        if (exc && exc != PROTO_NONE) env().handleException(exc, nullptr, out);
        return out.str().empty() ? std::string("failed") : out.str();
    }
};

TEST_F(ContextChain, AfterPropertyCalledFromNative) {
    // Attribute lookup runs a property getter through
    // PythonEnvironment::callObject, with the interpreter's s_threadContext
    // (an outer frame's context) as the context it is handed.
    const char* src =
        "class _A:\n"
        "    @property\n"
        "    def p(self):\n"
        "        return _chain_ok()\n"
        "def _f():\n"
        "    inside = _A().p\n"
        "    return inside and _chain_ok()\n"
        "assert _f(), 'frame lost after a property getter'\n";
    EXPECT_EQ(run(src, "<chain:property>"), "");
}

TEST_F(ContextChain, AfterGeneratorResumedFromNative) {
    // list() resumes the generator through PythonEnvironment::next.
    const char* src =
        "def _g():\n"
        "    yield 1\n"
        "    yield 2\n"
        "def _f():\n"
        "    inside = []\n"
        "    def _h():\n"
        "        for x in _g():\n"
        "            inside.append(_chain_ok())\n"
        "            yield x\n"
        "    items = list(_h())\n"
        "    return items == [1, 2] and all(inside) and _chain_ok()\n"
        "assert _f(), 'frame lost after a generator resumed by list()'\n";
    EXPECT_EQ(run(src, "<chain:generator>"), "");
}

TEST_F(ContextChain, AfterExecAndImport) {
    // exec and import run code objects in a context of their own
    // (runCodeObject).  Coverage for the second construction site: on the
    // defective build this one passed, because the bytecode hands
    // runCodeObject its own (current) frame unless an earlier native
    // re-entry had already broken the chain.
    const char* src =
        "def _f():\n"
        "    ns = {}\n"
        "    exec('def k():\\n    return _chain_ok()\\nr = k()', ns)\n"
        "    import colorsys\n"
        "    return ns['r'] and _chain_ok()\n"
        "assert _f(), 'frame lost after exec or import'\n";
    EXPECT_EQ(run(src, "<chain:exec>"), "");
}

TEST(ContextChainEnvironment, RootContextIsChainedAndUnwound) {
    // A PythonEnvironment's root context is chained onto the context that was
    // current when it was created, and destroying the environment makes that
    // context current again.
    proto::ProtoContext* before = nullptr;
    proto::ProtoContext* root = nullptr;
    proto::ProtoContext* rootPrevious = nullptr;
    {
        // The shared environment of the fixture above (if it ran) leaves a
        // context current on this thread; a second environment nests on it.
        PythonEnvironment outer(STDLIB_PATH);
        before = threadCurrent(outer.getContext());
        {
            PythonEnvironment inner(STDLIB_PATH);
            root = inner.getContext();
            rootPrevious = root->previous;
            EXPECT_EQ(threadCurrent(root), root);
        }
        EXPECT_EQ(rootPrevious, before) << "the root context skipped the current context";
        EXPECT_EQ(threadCurrent(outer.getContext()), before)
            << "destroying the environment left another context current";
    }
}

}  // namespace
