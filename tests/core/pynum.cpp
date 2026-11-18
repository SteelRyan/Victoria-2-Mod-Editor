// core/pynum against Python itself: every line of tests/core/pynum_cases.txt (made by tools/make_pycases.py).
// Usage: med_pynum <pynum_cases.txt>   — prints the differences, exit code = their number (capped at 255)
#include "nopopup.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/localisation.h"

#include <QFile>
#include <QStringList>
#include <array>
#include <charconv>
#include <cstdio>

static double num(const QString &s)
{
    if (s == u"inf")
        return HUGE_VAL;
    if (s == u"-inf")
        return -HUGE_VAL;
    const QByteArray b = s.toLatin1();
    double v = 0;
    std::from_chars(b.constData(), b.constData() + b.size(), v);
    return v;
}

// Python's "unicode_escape" back to the text
static QString unesc(const QString &s)
{
    QString out;
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] != u'\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        const QChar c = s[++i];
        if (c == u't') out += u'\t';
        else if (c == u'n') out += u'\n';
        else if (c == u'r') out += u'\r';
        else if (c == u'\\') out += u'\\';
        else if (c == u'x') { out += QChar(char16_t(s.mid(i + 1, 2).toUInt(nullptr, 16))); i += 2; }
        else if (c == u'u') { out += QChar(char16_t(s.mid(i + 1, 4).toUInt(nullptr, 16))); i += 4; }
        else if (c == u'U') { out += QString::fromUcs4(std::array<char32_t, 1>{char32_t(s.mid(i + 1, 8).toUInt(nullptr, 16))}.data(), 1); i += 8; }
        else { out += u'\\'; out += c; }
    }
    return out;
}

// Python's "unicode_escape" (what make_pycases.py writes for texts)
static QString esc(const QString &s)
{
    QString out;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        const char16_t u = c.unicode();
        if (QChar::isHighSurrogate(u) && i + 1 < s.size() && QChar::isLowSurrogate(s[i + 1].unicode())) {
            out += QStringLiteral("\\U%1").arg(uint(QChar::surrogateToUcs4(u, s[i + 1].unicode())), 8, 16, QChar(u'0'));
            ++i;
            continue;
        }
        if (u == u'\t') out += QStringLiteral("\\t");
        else if (u == u'\n') out += QStringLiteral("\\n");
        else if (u == u'\r') out += QStringLiteral("\\r");
        else if (u == u'\\') out += QStringLiteral("\\\\");
        else if (u >= 0x20 && u < 0x7F) out += c;
        else if (u < 0x100) out += QStringLiteral("\\x%1").arg(int(u), 2, 16, QChar(u'0'));
        else out += QStringLiteral("\\u%1").arg(int(u), 4, 16, QChar(u'0'));
    }
    return out;
}

static const QStringList TEXT_KINDS = {QStringLiteral("lower"), QStringLiteral("upper"), QStringLiteral("capitalize"), QStringLiteral("title"), QStringLiteral("strip"),
                                       QStringLiteral("lstrip"), QStringLiteral("rstrip"), QStringLiteral("split"), QStringLiteral("split_space"), QStringLiteral("split_1"),
                                       QStringLiteral("splitlines"), QStringLiteral("loc_clean")};

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 2;
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly))
        return 2;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts);
    int bad = 0, n = 0, skipped = 0;
    for (const QString &line : lines) {
        const QStringList p = line.split(u'\t');
        if (p.size() != 3)
            continue;
        const QString &kind = p[0], &in = p[1], &want = p[2];
        QString got;
        try {
            if (kind == u"repr") got = py::repr_float(num(in));
            else if (kind.startsWith(u'f') && kind.size() == 2) got = py::fmt_f(num(in), kind.mid(1).toInt());
            else if (kind.startsWith(u'g') && kind.size() == 2) got = py::fmt_g(num(in), kind.mid(1).toInt());
            else if (kind == u"round") got = QString::number(py::round_int(num(in)));
            else if (kind.startsWith(u"round")) got = py::repr_float(py::round_to(num(in), kind.mid(5).toInt()));
            else if (kind == u"trunc") got = QString::number(py::trunc_int(num(in)));
            else if (kind == u"commas0") got = py::fmt_commas(num(in), 0);
            else if (kind == u"commas2") got = py::fmt_commas(num(in), 2);
            else if (kind == u"idiv" || kind == u"imod") {
                const QStringList ab = in.split(u' ');
                const long long a = ab[0].toLongLong(), b = ab[1].toLongLong();
                got = QString::number(kind == u"idiv" ? py::floordiv(a, b) : py::mod(a, b));
            } else if (kind == u"fdiv" || kind == u"fmod") {
                const QStringList ab = in.split(u' ');
                const double a = num(ab[0]), b = num(ab[1]);
                got = py::repr_float(kind == u"fdiv" ? py::floordiv(a, b) : py::mod(a, b));
            } else if (kind == u"loc_clean") got = localisation::clean(unesc(in));
            else if (kind == u"lower") got = py::lower(unesc(in));
            else if (kind == u"upper") got = py::upper(unesc(in));
            else if (kind == u"capitalize") got = py::capitalize(unesc(in));
            else if (kind == u"title") got = py::title(unesc(in));
            else if (kind == u"strip") got = py::strip(unesc(in));
            else if (kind == u"lstrip") got = py::lstrip(unesc(in));
            else if (kind == u"rstrip") got = py::rstrip(unesc(in));
            else if (kind == u"split") got = py::split(unesc(in)).join(QChar(0x1f));
            else if (kind == u"split_space") got = py::split(unesc(in), QStringLiteral(" ")).join(QChar(0x1f));
            else if (kind == u"split_1") got = py::split(unesc(in), QStringLiteral(" "), 1).join(QChar(0x1f));
            else if (kind == u"splitlines") got = py::splitlines(unesc(in)).join(QChar(0x1f));
            else if (kind == u"isalnum") got = py::isalnum(unesc(in)) ? QStringLiteral("True") : QStringLiteral("False");
            else if (kind == u"isspace") got = py::isspace(unesc(in)) ? QStringLiteral("True") : QStringLiteral("False");
            else if (kind == u"int") got = QString::number(py::to_int(unesc(in)));
            else if (kind == u"isdigit") got = py::isdigit(unesc(in)) ? QStringLiteral("True") : QStringLiteral("False");
            else if (kind == u"float") {
                auto v = py::to_float(unesc(in));
                got = v ? py::repr_float(*v) : QStringLiteral("!ValueError");
            } else {                              // (an unknown kind is a failure, never skipped silently)
                std::printf("UNKNOWN case kind %s\n", qPrintable(kind));
                ++bad;
                continue;
            }
        } catch (const py::ValueError &) {
            got = QStringLiteral("!ValueError");
        }
        catch (const std::overflow_error &) {      // beyond 64-bit ints: out of the port's range, said so
            if (want.size() > 18) { ++skipped; continue; }
            got = QStringLiteral("!overflow");
        }
        ++n;
        if (TEXT_KINDS.contains(kind))
            got = esc(got);
        if (got != want) {
            if (++bad <= 40)
                std::printf("DIFF %s(%s): python %s, c++ %s\n", qPrintable(kind), qPrintable(in), qPrintable(want),
                            qPrintable(got));
        }
    }
    std::printf("%d cases, %d different, %d beyond 64-bit ints (out of range)\n", n, bad, skipped);
    return bad > 255 ? 255 : bad;
}
