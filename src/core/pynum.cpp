// Python's number rules (see pynum.h). Built on std::to_chars / std::from_chars: exact, shortest-correct,
// independent of the locale.
#include "core/pynum.h"

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace py {

static QString lat(const char *b, const char *e) { return QString::fromLatin1(b, e - b); }

// CPython (Modules/mathmodule.c): vector_norm with its DoubleLength helpers, for the two arguments of hypot
namespace {
struct DoubleLength {
    double hi, lo;
};
DoubleLength dl_fast_sum(double a, double b)
{
    const double x = a + b;
    const double y = (a - x) + b;
    return {x, y};
}
DoubleLength dl_mul(double x, double y)
{
    const double z = x * y;
    const double zz = std::fma(x, y, -z);
    return {z, zz};
}
double vector_norm(int n, double *vec, double max, bool found_nan)
{
    double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
    if (std::isinf(max))
        return max;
    if (found_nan)
        return std::nan("");
    if (max == 0.0 || n <= 1)
        return max;
    int max_e = 0;
    std::frexp(max, &max_e);
    if (max_e < -1023) {                              // (ldexp(1.0, -max_e) would overflow)
        for (int i = 0; i < n; ++i)
            vec[i] /= DBL_MIN;
        return DBL_MIN * vector_norm(n, vec, max / DBL_MIN, found_nan);
    }
    const double scale = std::ldexp(1.0, -max_e);
    for (int i = 0; i < n; ++i) {
        double x = vec[i];
        x *= scale;                                   // lossless scaling
        const DoubleLength pr = dl_mul(x, x);         // lossless squaring
        const DoubleLength sm = dl_fast_sum(csum, pr.hi);
        csum = sm.hi;
        frac1 += pr.lo;
        frac2 += sm.lo;
    }
    double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
    const DoubleLength pr = dl_mul(-h, h);
    const DoubleLength sm = dl_fast_sum(csum, pr.hi);
    csum = sm.hi;
    frac1 += pr.lo;
    frac2 += sm.lo;
    const double x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);                               // differential correction
    return h / scale;
}
}  // namespace

double hypot(double x, double y)
{
    double v[2] = {std::fabs(x), std::fabs(y)};
    const bool nan = std::isnan(v[0]) || std::isnan(v[1]);
    double max = 0.0;
    for (double a : v)
        if (a > max)
            max = a;
    return vector_norm(2, v, max, nan);
}

std::optional<double> to_float(const QString &s0)
{
    const QString s = strip(s0);
    if (s.isEmpty())
        return std::nullopt;
    // inf / infinity / nan, with an optional sign, any case
    QString low = s.toLower();
    bool neg = false;
    QString body = low;
    if (body.startsWith(u'+') || body.startsWith(u'-')) {
        neg = body.startsWith(u'-');
        body = body.mid(1);
    }
    if (body == u"inf" || body == u"infinity")
        return neg ? -HUGE_VAL : HUGE_VAL;
    if (body == u"nan")
        return std::nan("");
    // digits [. digits] [e [sign] digits], single underscores only between two digits
    std::string a;
    a.reserve(size_t(s.size()));
    qsizetype i = 0;
    const qsizetype n = s.size();
    auto digit = [&](qsizetype k) { return k < n && s[k] >= u'0' && s[k] <= u'9'; };
    if (i < n && (s[i] == u'+' || s[i] == u'-'))
        a.push_back(char(s[i++].unicode()));
    auto run = [&]() {           // digits with single underscores between them; returns how many digits
        int count = 0;
        while (i < n) {
            if (digit(i)) {
                a.push_back(char(s[i].unicode()));
                ++count;
                ++i;
            } else if (s[i] == u'_' && count > 0 && digit(i + 1)) {
                ++i;
            } else {
                break;
            }
        }
        return count;
    };
    int before = run(), after = 0;
    if (i < n && s[i] == u'.') {
        a.push_back('.');
        ++i;
        after = run();
    }
    if (before + after == 0)
        return std::nullopt;
    if (i < n && (s[i] == u'e' || s[i] == u'E')) {
        a.push_back('e');
        ++i;
        if (i < n && (s[i] == u'+' || s[i] == u'-'))
            a.push_back(char(s[i++].unicode()));
        if (run() == 0)
            return std::nullopt;
    }
    if (i != n)
        return std::nullopt;
    double v = 0;
    const char *b = a.data();
    if (*b == '+')
        ++b;
    auto r = std::from_chars(b, a.data() + a.size(), v);
    if (r.ec == std::errc::result_out_of_range) {        // Python: 1e999 -> inf, 1e-999 -> 0.0
        const bool negv = a[0] == '-';
        const auto epos = a.find('e');
        const bool big = epos != std::string::npos && a[epos + 1] != '-';
        return big ? (negv ? -HUGE_VAL : HUGE_VAL) : (negv ? -0.0 : 0.0);
    }
    if (r.ec != std::errc() || r.ptr != a.data() + a.size())
        return std::nullopt;
    return v;
}

double round_to(double x, int ndigits)
{
    if (std::isnan(x) || std::isinf(x) || x == 0)
        return x;
    if (ndigits < 0) {
        // Python: round(1234.5, -2) == 1200.0 (half to even on the exact value). Exactness kept for the
        // magnitudes a mod uses: scale by a power of ten that is itself exact.
        const double p = std::pow(10.0, -ndigits);
        return std::nearbyint(x / p) * p;
    }
    if (ndigits > 300)
        return x;
    char buf[400];
    auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, ndigits);   // exact, ties to even
    double v = 0;
    std::from_chars(buf, r.ptr, v);
    return v;
}

QString repr_float(double x)
{
    if (std::isnan(x))
        return QStringLiteral("nan");
    if (std::isinf(x))
        return x < 0 ? QStringLiteral("-inf") : QStringLiteral("inf");
    // the shortest digits that read back the same, in scientific form: [-]d[.ddd]e[+-]xx
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);
    std::string s(buf, r.ptr);
    const bool neg = s[0] == '-';
    if (neg)
        s.erase(0, 1);
    const auto epos = s.find('e');
    const int exp = std::stoi(s.substr(epos + 1));
    std::string digits = s.substr(0, epos);
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    std::string out;
    if (exp >= -4 && exp < 16) {                     // Python's repr: fixed notation in this range
        if (exp >= 0) {
            if (int(digits.size()) <= exp + 1) {
                out = digits + std::string(size_t(exp + 1 - int(digits.size())), '0') + ".0";
            } else {
                out = digits.substr(0, size_t(exp + 1)) + "." + digits.substr(size_t(exp + 1));
            }
        } else {
            out = "0." + std::string(size_t(-exp - 1), '0') + digits;
        }
    } else {                                         // d[.ddd]e+XX, at least two exponent digits
        out = digits.substr(0, 1);
        if (digits.size() > 1)
            out += "." + digits.substr(1);
        char e[16];
        std::snprintf(e, sizeof e, "e%c%02d", exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
        out += e;
    }
    if (neg)
        out.insert(out.begin(), '-');
    return QString::fromLatin1(out.data(), qsizetype(out.size()));
}

QString fmt_f(double x, int digits)
{
    if (std::isnan(x))
        return QStringLiteral("nan");
    if (std::isinf(x))
        return x < 0 ? QStringLiteral("-inf") : QStringLiteral("inf");
    char buf[400];
    auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, digits);
    return lat(buf, r.ptr);
}

QString fmt_g(double x, int precision)
{
    if (std::isnan(x))
        return QStringLiteral("nan");
    if (std::isinf(x))
        return x < 0 ? QStringLiteral("-inf") : QStringLiteral("inf");
    if (precision == 0)
        precision = 1;
    char buf[400];
    auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::general, precision);   // = C's %g
    return lat(buf, r.ptr);
}

QString fmt_commas(double x, int digits)
{
    QString s = fmt_f(x, digits);
    if (s == u"nan" || s.endsWith(u"inf"))
        return s;
    const bool neg = s.startsWith(u'-');
    if (neg)
        s.remove(0, 1);
    qsizetype dot = s.indexOf(u'.');
    if (dot < 0)
        dot = s.size();
    for (qsizetype k = dot - 3; k > 0; k -= 3)
        s.insert(k, u',');
    return neg ? u'-' + s : s;
}

}  // namespace py
