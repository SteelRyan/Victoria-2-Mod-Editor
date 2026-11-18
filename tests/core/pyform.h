// The shared value form of the comparison dumps (tools/golden_g2.py: v()): None, True/False, ints, repr(float),
// repr(str), [a, b] for lists / tuples. Used by the C++ side of every G2 dump.
// (Order matters in C++: the plain writers first, then the templates, which find all of them.)
#pragma once
#include "core/pynum.h"
#include "core/pytext.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <vector>

namespace pyform {

using namespace Qt::StringLiterals;

template <typename T> QString v(const std::optional<T> &o);
template <typename T> QString v(const std::vector<T> &l);
template <typename A, typename B> QString v(const std::pair<A, B> &x);

[[maybe_unused]] inline QString v(std::nullptr_t) { return u"None"_s; }
inline QString v(bool b) { return b ? u"True"_s : u"False"_s; }
[[maybe_unused]] inline QString v(int i) { return QString::number(i); }
inline QString v(long long i) { return QString::number(i); }
inline QString v(double d) { return py::repr_float(d); }
inline QString v(const QString &s) { return py::repr_str(s); }
inline QString v(const std::tuple<int, int, int> &c)
{
    return u"[%1, %2, %3]"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}
inline QString v(const std::tuple<long long, long long, long long> &c)
{
    return u"[%1, %2, %3]"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}
inline QString v(const QStringList &l)
{
    QStringList p;
    for (const QString &s : l)
        p << v(s);
    return u'[' + p.join(u", "_s) + u']';
}

template <typename T>
QString v(const std::optional<T> &o) { return o ? v(*o) : u"None"_s; }
template <typename T>
QString v(const std::vector<T> &l)
{
    QStringList p;
    for (const T &x : l)
        p << v(x);
    return u'[' + p.join(u", "_s) + u']';
}
template <typename A, typename B>
QString v(const std::pair<A, B> &x) { return u'[' + v(x.first) + u", "_s + v(x.second) + u']'; }

// a list made on the spot: L(a, b, c) -> "[a, b, c]"
template <typename... T>
QString L(const T &...xs)
{
    QStringList p{v(xs)...};
    return u'[' + p.join(u", "_s) + u']';
}
// an already-formed list of parts
inline QString list(const QStringList &parts) { return u'[' + parts.join(u", "_s) + u']'; }

}  // namespace pyform
