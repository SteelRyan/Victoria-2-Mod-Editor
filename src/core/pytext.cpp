// Python's str methods (see pytext.h).
#include "core/pytext.h"

namespace py {

// a "cased" character for Python = Unicode's Cased property: Lu, Ll, Lt and Other_Lowercase / Other_Uppercase.
// Qt only knows the categories, so the Other_* ranges are listed (ª º are in latin-1 and in mods' names).
static bool other_cased(char16_t u)
{
    return u == 0xAA || u == 0xBA || (u >= 0x2B0 && u <= 0x2B8) || u == 0x2C0 || u == 0x2C1 ||
           (u >= 0x2E0 && u <= 0x2E4) || u == 0x345 || u == 0x37A || (u >= 0x1D2C && u <= 0x1D6A) || u == 0x1D78 ||
           (u >= 0x1D9B && u <= 0x1DBF) || u == 0x2071 || u == 0x207F || (u >= 0x2090 && u <= 0x209C) ||
           (u >= 0x2160 && u <= 0x217F) || (u >= 0x24B6 && u <= 0x24E9) || u == 0x2C7C || u == 0x2C7D ||
           u == 0xA69C || u == 0xA69D || u == 0xA770 || u == 0xA7F8 || u == 0xA7F9 || (u >= 0xAB5C && u <= 0xAB5F);
}

static bool cased(QChar c)
{
    const auto cat = c.category();
    return cat == QChar::Letter_Uppercase || cat == QChar::Letter_Lowercase || cat == QChar::Letter_Titlecase ||
           other_cased(c.unicode());
}

QString lower(const QString &s) { return s.toLower(); }

QString upper(const QString &s) { return s.toUpper(); }

// one character to title case — Python uses the full mapping of SpecialCasing.txt where it gives several
// characters ('ß' -> "Ss", 'ŉ' -> "ʼN", 'ﬁ' -> "Fi"); otherwise the simple one ('ǆ' -> 'ǅ'). The Greek entries
// with accents are left out: a mod's files are latin-1.
static QString title_char(QChar c)
{
    switch (c.unicode()) {
    case 0x00DF: return QStringLiteral("Ss");
    case 0x0149: return QString(QChar(0x02BC)) + u'N';
    case 0x01F0: return QString(u'J') + QChar(0x030C);
    case 0x0587: return QString(QChar(0x0535)) + QChar(0x0582);
    case 0x1E96: return QString(u'H') + QChar(0x0331);
    case 0x1E97: return QString(u'T') + QChar(0x0308);
    case 0x1E98: return QString(u'W') + QChar(0x030A);
    case 0x1E99: return QString(u'Y') + QChar(0x030A);
    case 0x1E9A: return QString(u'A') + QChar(0x02BE);
    case 0xFB00: return QStringLiteral("Ff");
    case 0xFB01: return QStringLiteral("Fi");
    case 0xFB02: return QStringLiteral("Fl");
    case 0xFB03: return QStringLiteral("Ffi");
    case 0xFB04: return QStringLiteral("Ffl");
    case 0xFB05:
    case 0xFB06: return QStringLiteral("St");
    default: break;
    }
    const char32_t t = QChar::toTitleCase(char32_t(c.unicode()));
    return QString(QChar(char16_t(t)));
}

QString capitalize(const QString &s)
{
    if (s.isEmpty())
        return s;
    return title_char(s[0]) + s.mid(1).toLower();
}

QString title(const QString &s)
{
    QString out;
    out.reserve(s.size());
    bool prev_cased = false;
    for (QChar c : s) {
        if (cased(c)) {
            out += prev_cased ? QString(c).toLower() : title_char(c);
            prev_cased = true;
        } else {
            out += c;
            prev_cased = false;
        }
    }
    return out;
}

QString strip(const QString &s, const QString &chars)
{
    qsizetype a = 0, b = s.size();
    while (a < b && chars.contains(s[a]))
        ++a;
    while (b > a && chars.contains(s[b - 1]))
        --b;
    return s.mid(a, b - a);
}

QString lstrip(const QString &s)
{
    qsizetype a = 0;
    while (a < s.size() && is_py_space(s[a].unicode()))
        ++a;
    return s.mid(a);
}

QString rstrip(const QString &s)
{
    qsizetype b = s.size();
    while (b > 0 && is_py_space(s[b - 1].unicode()))
        --b;
    return s.left(b);
}

QStringList split(const QString &s)
{
    QStringList out;
    qsizetype i = 0;
    const qsizetype n = s.size();
    while (i < n) {
        while (i < n && is_py_space(s[i].unicode()))
            ++i;
        if (i >= n)
            break;
        const qsizetype j0 = i;
        while (i < n && !is_py_space(s[i].unicode()))
            ++i;
        out << s.mid(j0, i - j0);
    }
    return out;
}

QStringList split(const QString &s, const QString &sep, int maxsplit)
{
    if (sep.isEmpty())
        throw ValueError("empty separator");
    QStringList out;
    qsizetype from = 0;
    int done = 0;
    while (maxsplit < 0 || done < maxsplit) {
        const qsizetype k = s.indexOf(sep, from);
        if (k < 0)
            break;
        out << s.mid(from, k - from);
        from = k + sep.size();
        ++done;
    }
    out << s.mid(from);
    return out;
}

// line boundaries of str.splitlines(): \n \r \r\n \v \f \x1c \x1d \x1e \x85
static bool line_break(char16_t u)
{
    return u == u'\n' || u == u'\r' || u == 0x0B || u == 0x0C || u == 0x1C || u == 0x1D || u == 0x1E || u == 0x85 ||
           u == 0x2028 || u == 0x2029;
}

QStringList splitlines(const QString &s)
{
    QStringList out;
    qsizetype i = 0, start = 0;
    const qsizetype n = s.size();
    while (i < n) {
        const char16_t u = s[i].unicode();
        if (line_break(u)) {
            out << s.mid(start, i - start);
            i += (u == u'\r' && i + 1 < n && s[i + 1] == u'\n') ? 2 : 1;
            start = i;
        } else {
            ++i;
        }
    }
    if (start < n)
        out << s.mid(start);
    return out;
}

QStringList splitlines_keep(const QString &s)
{
    QStringList out;
    qsizetype i = 0, start = 0;
    const qsizetype n = s.size();
    while (i < n) {
        const char16_t u = s[i].unicode();
        if (line_break(u)) {
            i += (u == u'\r' && i + 1 < n && s[i + 1] == u'\n') ? 2 : 1;
            out << s.mid(start, i - start);
            start = i;
        } else {
            ++i;
        }
    }
    if (start < n)
        out << s.mid(start);
    return out;
}

bool is_word_char(QChar c) { return c == u'_' || c.isLetterOrNumber(); }

bool isalnum(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s) {
        // Python: isalpha (L*) or isdecimal / isdigit / isnumeric (N*)
        if (!c.isLetter() && !c.isNumber())
            return false;
    }
    return true;
}

bool isprintable(QChar c)
{
    if (c == u' ')
        return true;
    switch (c.category()) {
    case QChar::Other_Control: case QChar::Other_Format: case QChar::Other_Surrogate: case QChar::Other_PrivateUse:
    case QChar::Other_NotAssigned: case QChar::Separator_Line: case QChar::Separator_Paragraph:
    case QChar::Separator_Space:
        return false;
    default:
        return true;
    }
}

QString repr_str(const QString &s)
{
    const QChar q = (s.contains(u'\'') && !s.contains(u'"')) ? QChar(u'"') : QChar(u'\'');
    QString out(q);
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        const char16_t u = c.unicode();
        if (c.isHighSurrogate() && i + 1 < s.size() && s[i + 1].isLowSurrogate()) {   // one code point beyond the BMP
            const char32_t cp = QChar::surrogateToUcs4(c, s[i + 1]);
            switch (QChar::category(cp)) {
            case QChar::Other_Control: case QChar::Other_Format: case QChar::Other_Surrogate:
            case QChar::Other_PrivateUse: case QChar::Other_NotAssigned: case QChar::Separator_Line:
            case QChar::Separator_Paragraph: case QChar::Separator_Space:
                out += QStringLiteral("\\U%1").arg(uint(cp), 8, 16, QChar(u'0'));
                break;
            default:
                out += c;
                out += s[i + 1];
            }
            ++i;
            continue;
        }
        if (c == q || u == u'\\') { out += u'\\'; out += c; }
        else if (u == u'\t') out += QStringLiteral("\\t");
        else if (u == u'\n') out += QStringLiteral("\\n");
        else if (u == u'\r') out += QStringLiteral("\\r");
        else if (u < 0x20 || u == 0x7F) out += QStringLiteral("\\x%1").arg(int(u), 2, 16, QChar(u'0'));
        else if (u < 0x7F) out += c;
        else if (!isprintable(c)) {
            if (u <= 0xFF) out += QStringLiteral("\\x%1").arg(int(u), 2, 16, QChar(u'0'));
            else out += QStringLiteral("\\u%1").arg(int(u), 4, 16, QChar(u'0'));
        } else out += c;
    }
    out += q;
    return out;
}

bool isspace(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s)
        if (!is_py_space(c.unicode()))
            return false;
    return true;
}

}  // namespace py
