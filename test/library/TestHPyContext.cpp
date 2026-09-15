/**
 * Minimal HPy Phase 1 tests: HPyContext, handle table, FromPyObject/AsPyObject round-trip.
 */
#include <gtest/gtest.h>
#include <protoPython/HPyContext.h>
#include <protoCore.h>
#include <string>

using namespace protoPython;

namespace {

// Counts the objects pinned in root sets named "hpy-handles", the root set each
// HPyContext registers for its handles.
unsigned long countHandleRoots(proto::ProtoSpace& space) {
    unsigned long pinned = 0;
    space.forEachRootSet(
        [](void* user, proto::ProtoRootSet* rs) {
            if (std::string(rs->getName()) == "hpy-handles") {
                *static_cast<unsigned long*>(user) += rs->size();
            }
        },
        &pinned);
    return pinned;
}

}  // namespace

// A handle must keep its object reachable for the garbage collector: every
// open handle is pinned in the context's ProtoRootSet, HPy_Close releases the
// pin, and destroying the HPyContext releases the handles still open.
TEST(HPyContextTest, HandlesArePinnedAsGCRoots) {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    {
        HPyContext hctx(ctx);
        const proto::ProtoObject* list = ctx->newList()->appendLast(ctx, ctx->fromInteger(7))->asObject(ctx);
        HPy h = HPy_FromPyObject(&hctx, list);
        ASSERT_NE(h, 0u);
        EXPECT_EQ(countHandleRoots(space), 1u);
        EXPECT_EQ(HPy_AsPyObject(&hctx, h), list);

        HPy dup = HPy_Dup(&hctx, h);
        ASSERT_NE(dup, 0u);
        EXPECT_EQ(countHandleRoots(space), 2u);

        HPy_Close(&hctx, h);
        EXPECT_EQ(countHandleRoots(space), 1u);
        EXPECT_EQ(HPy_AsPyObject(&hctx, h), nullptr);   // a closed handle no longer resolves
        EXPECT_EQ(HPy_AsPyObject(&hctx, dup), list);

        HPy_Close(&hctx, h);                            // closing twice is harmless
        EXPECT_EQ(countHandleRoots(space), 1u);

        HPy reused = HPy_FromLong(&hctx, 1);            // recycles the closed slot
        EXPECT_EQ(reused, h);
        EXPECT_EQ(countHandleRoots(space), 2u);
        // dup and reused stay open: the context's destructor must release them.
    }
    EXPECT_EQ(countHandleRoots(space), 0u);
}

TEST(HPyContextTest, FromPyObjectAsPyObjectRoundTrip) {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    const proto::ProtoObject* obj = ctx->fromLong(42);
    ASSERT_NE(obj, nullptr);

    HPyContext hctx(ctx);
    HPy h = HPy_FromPyObject(&hctx, obj);
    ASSERT_NE(h, 0u);

    const proto::ProtoObject* back = HPy_AsPyObject(&hctx, h);
    EXPECT_EQ(back, obj);
    EXPECT_EQ(back->asLong(ctx), 42);

    HPy_Close(&hctx, h);
}

TEST(HPyContextTest, DupAndClose) {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    const proto::ProtoObject* obj = ctx->fromUTF8String("x");
    ASSERT_NE(obj, nullptr);

    HPyContext hctx(ctx);
    HPy h1 = HPy_FromPyObject(&hctx, obj);
    ASSERT_NE(h1, 0u);
    HPy h2 = HPy_Dup(&hctx, h1);
    EXPECT_NE(h2, 0u);

    HPy_Close(&hctx, h1);
    const proto::ProtoObject* still = HPy_AsPyObject(&hctx, h2);
    EXPECT_NE(still, nullptr);
    HPy_Close(&hctx, h2);
}

TEST(HPyContextTest, GetAttrAndType) {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    const proto::ProtoObject* obj = ctx->fromLong(1);
    ASSERT_NE(obj, nullptr);

    HPyContext hctx(ctx);
    HPy hObj = HPy_FromPyObject(&hctx, obj);
    ASSERT_NE(hObj, 0u);

    HPy hType = HPy_Type(&hctx, hObj);
    EXPECT_NE(hType, 0u);

    HPy_Close(&hctx, hType);
    HPy_Close(&hctx, hObj);
}

TEST(HPyContextTest, GetAttrMissingReturnsZero) {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    const proto::ProtoObject* obj = ctx->fromLong(5);
    ASSERT_NE(obj, nullptr);

    HPyContext hctx(ctx);
    HPy hObj = HPy_FromPyObject(&hctx, obj);
    ASSERT_NE(hObj, 0u);

    HPy hAttr = HPy_GetAttr(&hctx, hObj, "__nonexistent__");
    EXPECT_EQ(hAttr, 0u);

    HPy_Close(&hctx, hObj);
}
