/**
 * Minimal HPy Phase 1 tests: HPyContext, handle table, FromPyObject/AsPyObject round-trip.
 */
#include <gtest/gtest.h>
#include <protoPython/HPyContext.h>
#include <protoCore.h>

using namespace protoPython;

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
