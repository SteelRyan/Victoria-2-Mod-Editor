// Python's number rules, where C++ differs (PLAN.md §4.4). Every function here is tested against values Python
// itself printed (tests/core/pynum_cases.txt, made by tools/make_pycases.py).
#pragma once
#include <QString>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace py {

// Python raises ValueError; the port throws this (caught where the Python catches, else logged by notify)
struct ValueError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// str.isdigit(): Unicode "Digit" property — in latin-1: 0-9 and the superscripts ¹ ² ³
inline bool isdigit(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s) {
        const char16_t u = c.unicode();
        if (u > 0xFF) {
            if (!c.isDigit() && c.digitValue() < 0)
                return false;
            continue;
        }
        if (!((u >= u'0' && u <= u'9') || u == 0xB2 || u == 0xB3 || u == 0xB9))
            return false;
    }
    return true;
}

// Python's str.strip() with no argument: Unicode whitespace (incl. \x1c-\x1f, \x85, \xa0)
inline bool is_py_space(char16_t u)
{
    return u == u' ' || (u >= 0x09 && u <= 0x0D) || (u >= 0x1C && u <= 0x1F) || u == 0x85 || u == 0xA0 ||
           u == 0x1680 || (u >= 0x2000 && u <= 0x200A) || u == 0x2028 || u == 0x2029 || u == 0x202F ||
           u == 0x205F || u == 0x3000;
}

inline QString strip(const QString &s)
{
    qsizetype a = 0, b = s.size();
    while (a < b && is_py_space(s[a].unicode()))
        ++a;
    while (b > a && is_py_space(s[b - 1].unicode()))
        --b;
    return s.mid(a, b - a);
}

// int(str): surrounding whitespace, one sign, decimal digits (ASCII; Unicode Nd digits too, not superscripts),
// single underscores BETWEEN digits. Anything else: ValueError.
inline long long to_int(const QString &s0)
{
    const QString s = strip(s0);
    qsizetype i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == u'+' || s[i] == u'-')) {
        neg = s[i] == u'-';
        ++i;
    }
    if (i >= s.size())
        throw ValueError("invalid literal for int()");
    long long v = 0;
    bool prev_digit = false;
    for (; i < s.size(); ++i) {
        const QChar c = s[i];
        if (c == u'_') {
            if (!prev_digit || i + 1 >= s.size())
                throw ValueError("invalid literal for int()");
            prev_digit = false;
            continue;
        }
        int d = -1;
        if (c >= u'0' && c <= u'9')
            d = c.unicode() - u'0';
        else if (c.unicode() > 0xFF && c.category() == QChar::Number_DecimalDigit)
            d = c.digitValue();
        if (d < 0)
            throw ValueError("invalid literal for int()");
        v = v * 10 + d;
        prev_digit = true;
    }
    if (!prev_digit)
        throw ValueError("invalid literal for int()");
    return neg ? -v : v;
}

// float(str): whitespace, sign, inf / infinity / nan (any case), decimal with optional exponent, single
// underscores between digits. Anything else: nullopt (Python: ValueError).
std::optional<double> to_float(const QString &s0);

// round(x) for a float, giving an int: half to even. (round(2.5) == 2, round(-0.5) == 0)
inline long long round_int(double x)
{
    if (std::isnan(x) || std::isinf(x))
        throw ValueError("cannot convert float NaN or infinity to integer");
    if (std::fabs(x) >= 9.2233720368547758e18)     // (Python's ints are unbounded; a mod's numbers never are)
        throw std::overflow_error("int beyond 64 bits");
    return static_cast<long long>(std::nearbyint(x));   // default rounding mode = to nearest, ties to even
}

// round(x, n) for a float with n digits — Python rounds the exact decimal value of the double, half to even
double round_to(double x, int ndigits);

// int(x) for a float: toward zero
inline long long trunc_int(double x)
{
    if (std::isnan(x) || std::isinf(x))
        throw ValueError("cannot convert float NaN or infinity to integer");
    if (std::fabs(x) >= 9.2233720368547758e18)
        throw std::overflow_error("int beyond 64 bits");
    return static_cast<long long>(std::trunc(x));
}

// a // b and a % b for ints: toward minus infinity (C++ cuts toward zero)
inline long long floordiv(long long a, long long b)
{
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}
inline long long mod(long long a, long long b)
{
    long long r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}
// a // b and a % b for floats: CPython's float_divmod / float_rem — a zero remainder takes the sign of b
// ((-0.0) % 6.28 is 0.0, not -0.0), the quotient is snapped to the nearest integer (1 // 0.1 is 9.0, while
// floor(1 / 0.1) is 10.0). Found 2026-10-20: positions' text_rotation was written as -0.000000.
inline double mod(double a, double b)
{
    double r = std::fmod(a, b);
    if (r != 0) {
        if ((b < 0) != (r < 0))
            r += b;
    } else {
        r = std::copysign(0.0, b);
    }
    return r;
}
inline double floordiv(double a, double b)
{
    double r = std::fmod(a, b);
    double div = (a - r) / b;
    if (r != 0 && ((b < 0) != (r < 0)))
        div -= 1.0;
    if (div == 0)
        return std::copysign(0.0, a / b);
    double fl = std::floor(div);
    if (div - fl > 0.5)
        fl += 1.0;
    return fl;
}

// sum(xs) as Python 3.12+ adds them: ints exactly while only ints came; at the first float the int total and that
// float are added plainly, after it every item (float or int) by Neumaier's compensated sum; the compensation is
// added at the end when it is finite. sum([0.1] * 10) == 1.0, not 0.9999999999999999.
struct Sum {
    bool fl = false;
    long long i = 0;
    double f = 0.0, c = 0.0;
    void add(long long x)
    {
        if (!fl)
            i += x;
        else
            comp(static_cast<double>(x));
    }
    void add(int x) { add(static_cast<long long>(x)); }
    void add(double x)
    {
        if (!fl) {
            fl = true;
            f = static_cast<double>(i) + x;
        } else {
            comp(x);
        }
    }
    void comp(double x)
    {
        const double t = f + x;
        if (std::fabs(f) >= std::fabs(x))
            c += (f - t) + x;
        else
            c += (x - t) + f;
        f = t;
    }
    double value() const { return !fl ? static_cast<double>(i) : (c != 0.0 && std::isfinite(c)) ? f + c : f; }
    bool is_float() const { return fl; }
};
template <typename R> double sum(const R &xs)
{
    Sum s;
    for (const auto &x : xs)
        s.add(x);
    return s.value();
}

// math.hypot(x, y) as CPython computes it (its own vector_norm: scaled, squares and sum kept exact, one correction
// step) — not the C library's hypot, which can differ in the last bit
double hypot(double x, double y);

// repr(float) / str(float): the shortest text that reads back as the same double ("1.0", "0.1", "1e+16", "inf")
QString repr_float(double x);
// "%.Nf" % x  (as printf: exact decimal value, half to even on that exact value)
QString fmt_f(double x, int digits);
// "%g" % x  and "%.Ng" % x
QString fmt_g(double x, int precision = 6);
// "{:,.Nf}".format(x) — thousands separators
QString fmt_commas(double x, int digits);

}  // namespace py
