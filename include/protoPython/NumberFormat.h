#ifndef PROTOPYTHON_NUMBERFORMAT_H
#define PROTOPYTHON_NUMBERFORMAT_H

// The format specification mini-language for int and float (PEP 3101,
// CPython 3.14's Python/formatter_unicode.c), in one place: format(),
// f-strings, str.format, int.__format__, float.__format__ and the float
// conversions of the % operator all go through it.
//
//   [[fill]align][sign]["z"]["#"]["0"][width][grouping]["." [precision][grouping]][type]

#include <functional>
#include <string>

namespace protoPython {
namespace numfmt {

enum class Error { None, Value, Overflow };

struct Result {
    std::string text;            // the formatted value when err == None
    Error err = Error::None;
    std::string message;         // the exception message otherwise
};

/** repr(float): the shortest string that reads back as the same double. */
std::string floatRepr(double value);

/** format(value, spec) for a float. */
Result formatFloat(double value, const std::string& spec);

/**
 * format(value, spec) for an int of any size. `negative` is its sign;
 * `digits(base)` returns the digits of its magnitude in base 2, 8, 10 or 16
 * (lower case); `toDouble(out)` converts it as float() does, returning false
 * when it is out of the float range. `isBool` makes an empty spec spell
 * True / False.
 */
Result formatInt(bool negative, const std::function<std::string(int)>& digits,
                 const std::function<bool(double&)>& toDouble, const std::string& spec, bool isBool = false,
                 bool boolValue = false);

/** format(value, spec) for a str (UTF-8 text). */
Result formatString(const std::string& value, const std::string& spec);

/**
 * The % operator's float conversions (e E f F g G): `flags` are the
 * conversion's flag characters, `width` and `precision` -1 when absent.
 */
std::string percentFloat(double value, const std::string& flags, int width, int precision, char conversion);

} // namespace numfmt
} // namespace protoPython

#endif
