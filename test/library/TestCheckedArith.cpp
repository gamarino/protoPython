// protoPython::checkedMul64 / checkedShl64 (CheckedArith.h): the overflow
// checks behind the SmallInt fast paths and protopyc's emitted arithmetic. The
// MSVC build exercises the _mul128 implementation, the others the builtin.
#include <protoPython/CheckedArith.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <limits>

using protoPython::checkedMul64;
using protoPython::checkedShl64;

namespace {
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
}

TEST(CheckedArith, MulInRange) {
    std::int64_t r = 0;
    EXPECT_FALSE(checkedMul64(0, kMin, &r)); EXPECT_EQ(r, 0);
    EXPECT_FALSE(checkedMul64(-7, 6, &r)); EXPECT_EQ(r, -42);
    EXPECT_FALSE(checkedMul64(-7, -6, &r)); EXPECT_EQ(r, 42);
    EXPECT_FALSE(checkedMul64(3037000499LL, 3037000499LL, &r)); EXPECT_EQ(r, 9223372030926249001LL);
    EXPECT_FALSE(checkedMul64(kMax, 1, &r)); EXPECT_EQ(r, kMax);
    EXPECT_FALSE(checkedMul64(kMin, 1, &r)); EXPECT_EQ(r, kMin);
    EXPECT_FALSE(checkedMul64(-(std::int64_t{1} << 62), 2, &r)); EXPECT_EQ(r, kMin);
    EXPECT_FALSE(checkedMul64(kMax, -1, &r)); EXPECT_EQ(r, -kMax);
    // 56-bit SmallInt operands whose product fits.
    EXPECT_FALSE(checkedMul64(std::int64_t{1} << 30, std::int64_t{1} << 32, &r)); EXPECT_EQ(r, std::int64_t{1} << 62);
}

TEST(CheckedArith, MulOverflow) {
    std::int64_t r = 0;
    EXPECT_TRUE(checkedMul64(std::int64_t{1} << 40, std::int64_t{1} << 40, &r));  // 2**80, truncates to 0
    EXPECT_TRUE(checkedMul64(3037000500LL, 3037000500LL, &r));
    EXPECT_TRUE(checkedMul64(kMin, -1, &r));
    EXPECT_TRUE(checkedMul64(std::int64_t{1} << 62, 2, &r));
    EXPECT_TRUE(checkedMul64(-(std::int64_t{1} << 62), -2, &r));
    EXPECT_TRUE(checkedMul64(kMax, kMax, &r));
    EXPECT_TRUE(checkedMul64(kMin, kMin, &r));
    EXPECT_TRUE(checkedMul64(-(std::int64_t{1} << 53), std::int64_t{1} << 53, &r));
}

TEST(CheckedArith, ShiftLeft) {
    std::int64_t r = 0;
    EXPECT_FALSE(checkedShl64(1, 30, &r)); EXPECT_EQ(r, std::int64_t{1} << 30);
    EXPECT_FALSE(checkedShl64(-1, 62, &r)); EXPECT_EQ(r, -(std::int64_t{1} << 62));
    EXPECT_FALSE(checkedShl64(-1, 63, &r)); EXPECT_EQ(r, kMin);
    EXPECT_FALSE(checkedShl64(0, 70, &r)); EXPECT_EQ(r, 0);
    EXPECT_TRUE(checkedShl64(1, 63, &r));
    EXPECT_TRUE(checkedShl64(3, 62, &r));
    EXPECT_TRUE(checkedShl64(-3, 62, &r));
    EXPECT_TRUE(checkedShl64(1, 64, &r));
}
