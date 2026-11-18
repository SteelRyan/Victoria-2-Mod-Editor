// Python's re for the patterns the editor uses, on QRegularExpression: lines end at "\n" only ((*LF)), \d \w \b by
// Unicode. Python's \s / \S also take \x1c-\x1f: write PY_S / PY_NS in a pattern where that can matter.
#pragma once
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <functional>

namespace py {

inline const QString PY_S = QStringLiteral("[\\s\\x1c-\\x1f]");
inline const QString PY_NS = QStringLiteral("[^\\s\\x1c-\\x1f]");

inline const QRegularExpression &rx(const QString &pattern, bool icase = false)
{
    static QHash<QString, QRegularExpression> cache[2];
    auto &c = cache[icase ? 1 : 0];
    auto it = c.find(pattern);
    if (it == c.end()) {
        QRegularExpression::PatternOptions o = QRegularExpression::UseUnicodePropertiesOption;
        if (icase)
            o |= QRegularExpression::CaseInsensitiveOption;
        it = c.insert(pattern, QRegularExpression(QStringLiteral("(*LF)") + pattern, o));
    }
    return *it;
}
// re.match: anchored at `from`
inline QRegularExpressionMatch re_match(const QString &pattern, const QString &s, qsizetype from = 0, bool icase = false)
{
    return rx(pattern, icase).match(s, from, QRegularExpression::NormalMatch,
                                    QRegularExpression::AnchorAtOffsetMatchOption);
}
inline QRegularExpressionMatch re_search(const QString &pattern, const QString &s, qsizetype from = 0, bool icase = false)
{
    return rx(pattern, icase).match(s, from);
}
// re.sub(pattern, fn, s, count): each match replaced by fn(match) (count 0: all)
inline QString re_sub(const QString &pattern, const QString &s,
                      const std::function<QString(const QRegularExpressionMatch &)> &fn, qsizetype count = 0)
{
    QString out;
    qsizetype last = 0, n = 0;
    auto it = rx(pattern).globalMatch(s);
    while (it.hasNext() && (count == 0 || n < count)) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart(0) - last) + fn(m);
        last = m.capturedEnd(0);
        ++n;
    }
    return out + s.mid(last);
}

}  // namespace py
