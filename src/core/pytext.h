// Python's str methods, where Qt's differ (PLAN.md §4.4). Tested against Python itself on every latin-1 character
// and on the mods' own words (tests/core/pytext_cases.txt, made by tools/make_pycases.py).
#pragma once
#include "core/pynum.h"   // strip, is_py_space

#include <QString>
#include <QStringList>
#include <algorithm>

namespace py {

QString lower(const QString &s);        // str.lower()  (full Unicode mapping)
QString upper(const QString &s);        // str.upper()  ('ß' -> "SS")
QString capitalize(const QString &s);   // str.capitalize(): first character title-cased, the rest lower
QString title(const QString &s);        // str.title(): a word = a run of cased characters
QString strip(const QString &s, const QString &chars);     // str.strip(chars)
QString lstrip(const QString &s);
QString rstrip(const QString &s);
QStringList split(const QString &s);                        // str.split(): runs of whitespace, no empty parts
QStringList split(const QString &s, const QString &sep, int maxsplit = -1);   // str.split(sep, maxsplit)
QStringList splitlines(const QString &s);                   // str.splitlines() without keepends
QStringList splitlines_keep(const QString &s);              // str.splitlines(True): each line with its break
bool is_word_char(QChar c);                                 // re's \w for str patterns: alnum (Unicode) or _
bool isalnum(const QString &s);
bool isspace(const QString &s);
QString repr_str(const QString &s);    // repr(str): quotes and escapes exactly as Python prints them
bool isprintable(QChar c);             // str.isprintable() for one character

// s[a:b] / s[:b] / s[a:] with Python's rules: a negative index counts from the end, out of range is cut to the ends
// (Qt's left(-1) / mid(-1) mean "everything" instead)
inline qsizetype py_index(qsizetype i, qsizetype n) { return i < 0 ? std::max<qsizetype>(0, n + i) : std::min(i, n); }
inline QString slice(const QString &s, qsizetype a, qsizetype b)
{
    a = py_index(a, s.size());
    b = py_index(b, s.size());
    return b > a ? s.mid(a, b - a) : QString();
}
inline QString head(const QString &s, qsizetype b) { return slice(s, 0, b); }        // s[:b]
inline QString tail(const QString &s, qsizetype a) { return slice(s, a, s.size()); } // s[a:]
// s.rfind(sub, 0, end) for one character (end may be negative)
inline qsizetype rfind(const QString &s, QChar c, qsizetype end)
{
    end = py_index(end, s.size());
    return end <= 0 ? -1 : s.lastIndexOf(c, end - 1);
}

}  // namespace py
