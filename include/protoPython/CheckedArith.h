// CheckedArith.h — 64-bit integer arithmetic with overflow detection, the same
// on every compiler protoPython supports.
//
// GCC and Clang provide __builtin_mul_overflow; MSVC provides _mul128. The
// interpreter's SmallInt fast paths and the C++ that protopyc emits use these
// helpers instead of either, so neither depends on a 128-bit integer type
// (MSVC has none; its standard library's std::_Signed128 is internal) or on
// `long` being 64 bits (it is 32 bits on Windows).
#pragma once

#include <cstdint>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace protoPython {

// a * b. Returns true, and leaves *result unspecified, when the product does
// not fit in 64 bits; otherwise stores it and returns false.
inline bool checkedMul64(std::int64_t a, std::int64_t b, std::int64_t* result) {
#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_AMD64))
    std::int64_t high = 0;
    const std::int64_t low = _mul128(a, b, &high);
    *result = low;
    // The product fits when the high half is the sign extension of the low one.
    return high != (low < 0 ? -1 : 0);
#elif defined(_MSC_VER) && !defined(__clang__)
    // MSVC without _mul128 (ARM64): exact through the unsigned high product.
    const bool negative = (a < 0) != (b < 0);
    const std::uint64_t ua = a < 0 ? 0 - static_cast<std::uint64_t>(a) : static_cast<std::uint64_t>(a);
    const std::uint64_t ub = b < 0 ? 0 - static_cast<std::uint64_t>(b) : static_cast<std::uint64_t>(b);
    const std::uint64_t high = __umulh(ua, ub);
    const std::uint64_t low = ua * ub;
    const std::uint64_t limit = negative ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 63) - 1;
    if (high != 0 || low > limit) return true;
    *result = negative ? static_cast<std::int64_t>(0 - low) : static_cast<std::int64_t>(low);
    return false;
#else
    return __builtin_mul_overflow(a, b, result);
#endif
}

// a << shift for 0 <= shift < 64. Returns true when bits (or the sign) would be
// lost; otherwise stores the shifted value and returns false.
inline bool checkedShl64(std::int64_t a, unsigned shift, std::int64_t* result) {
    if (shift >= 64) {
        if (a != 0) return true;
        *result = 0;
        return false;
    }
    const std::int64_t shifted = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) << shift);
    // C++20: >> of a negative value is an arithmetic shift.
    if ((shifted >> shift) != a) return true;
    *result = shifted;
    return false;
}

} // namespace protoPython
