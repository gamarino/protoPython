// TimeConv.h — conversions between seconds since the epoch and broken-down
// time that never crash and behave the same on every platform.
//
// The C runtime's gmtime()/localtime() return NULL for a value they cannot
// represent (Microsoft's C runtime: anything before 1970 or after 3000-12-31),
// and callers dereferenced the result. Here:
//   - utcTime() computes UTC fields itself (days-from-civil arithmetic), so
//     every year that fits a struct tm converts, on every platform;
//   - localTime() asks the C runtime (it alone knows the time-zone rules) with
//     the reentrant call, and reports its error instead of crashing.
#pragma once

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace protoPython {
namespace timeconv {

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's
// days_from_civil). Valid for every year a 64-bit day count can hold.
inline std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// The date of a day count since 1970-01-01 (civil_from_days).
inline void civilFromDays(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2);
}

inline bool isLeapYear(std::int64_t y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

// Day of the week, Sunday = 0 (struct tm's convention), of a day count.
inline int weekdayFromDays(std::int64_t z) {
    return static_cast<int>(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

// Python's float/int seconds to whole seconds, rounding towards negative
// infinity as CPython's time functions do. False when the value is not finite
// or does not fit the platform's time_t (CPython: OverflowError, "timestamp out
// of range for platform time_t").
inline bool secondsToTimeT(double seconds, std::int64_t& out) {
    if (!std::isfinite(seconds)) return false;
    const double whole = std::floor(seconds);
    // 2**63 is exactly representable; time_t is 64 bits on every supported platform.
    if (whole < -9223372036854775808.0 || whole >= 9223372036854775808.0) return false;
    out = static_cast<std::int64_t>(whole);
    return true;
}

// UTC broken-down time of `t` seconds since the epoch. Returns 0, or EOVERFLOW
// when the year does not fit struct tm's int (CPython reports that errno too).
inline int utcTime(std::int64_t t, struct tm* out) {
    std::int64_t days = t / 86400;
    std::int64_t secs = t % 86400;
    if (secs < 0) { secs += 86400; days -= 1; }
    std::int64_t year;
    unsigned month, day;
    civilFromDays(days, year, month, day);
    if (year - 1900 > INT_MAX || year - 1900 < INT_MIN) return EOVERFLOW;
    std::memset(out, 0, sizeof(*out));
    out->tm_year = static_cast<int>(year - 1900);
    out->tm_mon = static_cast<int>(month) - 1;
    out->tm_mday = static_cast<int>(day);
    out->tm_hour = static_cast<int>(secs / 3600);
    out->tm_min = static_cast<int>((secs % 3600) / 60);
    out->tm_sec = static_cast<int>(secs % 60);
    out->tm_wday = weekdayFromDays(days);
    out->tm_yday = static_cast<int>(days - daysFromCivil(year, 1, 1));
    out->tm_isdst = 0;
    return 0;
}

// Local broken-down time of `t` from the C runtime. Returns 0 or the errno the
// C runtime reports (EINVAL on Windows outside 1970..3000, EOVERFLOW
// elsewhere for a year beyond int).
inline int localTime(std::int64_t t, struct tm* out) {
    const time_t tt = static_cast<time_t>(t);
    if (static_cast<std::int64_t>(tt) != t) return EOVERFLOW;
#if defined(_WIN32)
    const errno_t err = localtime_s(out, &tt);
    return err == 0 ? 0 : static_cast<int>(err);
#else
    errno = 0;
    if (localtime_r(&tt, out) == nullptr) return errno != 0 ? errno : EOVERFLOW;
    return 0;
#endif
}

} // namespace timeconv
} // namespace protoPython
