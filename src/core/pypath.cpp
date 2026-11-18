// Python's ntpath, translated from CPython's Lib/ntpath.py (the parts the editor uses).
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDir>
#include <iterator>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace pypath {

QString drive_of(const QString &p)
{
    if (p.size() >= 2 && p[1] == u':')
        return p.left(2);
    // UNC: \\server\share
    if (p.size() > 2 && is_sep(p[0]) && is_sep(p[1]) && !is_sep(p[2])) {
        qsizetype i = 2;
        while (i < p.size() && !is_sep(p[i]))
            ++i;
        if (i >= p.size())
            return p;
        qsizetype j = i + 1;
        while (j < p.size() && !is_sep(p[j]))
            ++j;
        return p.left(j);
    }
    return {};
}

QString join(const QString &a, const QStringList &parts)
{
    // ntpath.join: a part with a root (or a drive) starts over; same drive keeps it
    QString result_drive = drive_of(a);
    QString result_path = a.mid(result_drive.size());
    for (const QString &p : parts) {
        const QString p_drive = drive_of(p);
        const QString p_path = p.mid(p_drive.size());
        if (!p_path.isEmpty() && is_sep(p_path[0])) {            // second path is absolute
            if (!p_drive.isEmpty() || result_drive.isEmpty())
                result_drive = p_drive;
            result_path = p_path;
            continue;
        }
        if (!p_drive.isEmpty() && p_drive != result_drive) {
            if (p_drive.toLower() != result_drive.toLower()) {   // different drives: the second wins
                result_drive = p_drive;
                result_path = p_path;
                continue;
            }
            result_drive = p_drive;                               // same drive, other case
        }
        if (!result_path.isEmpty() && !is_sep(result_path.back()))
            result_path += u'\\';
        result_path += p_path;
    }
    // a relative path after a bare drive: "C:" + "x" -> "C:x"; after "C:\" keeps the separator
    if (!result_path.isEmpty() && !is_sep(result_path[0]) && !result_drive.isEmpty() && !result_drive.endsWith(u':'))
        return result_drive + u'\\' + result_path;
    return result_drive + result_path;
}

QString normpath(const QString &path)
{
    QString p = path;
    p.replace(u'/', u'\\');
    QString prefix = drive_of(p);
    p = p.mid(prefix.size());
    if (p.startsWith(u'\\')) {
        prefix += u'\\';
        while (p.startsWith(u'\\'))
            p.remove(0, 1);
    }
    const QStringList comps0 = p.split(u'\\');
    QStringList comps;
    for (const QString &c : comps0) {
        if (c.isEmpty() || c == u".")
            continue;
        if (c == u"..") {
            if (!comps.isEmpty() && comps.back() != u"..")
                comps.removeLast();
            else if (prefix.endsWith(u'\\'))
                continue;                                     // (".." at the root stays at the root)
            else
                comps << c;
            continue;
        }
        comps << c;
    }
    if (prefix.isEmpty() && comps.isEmpty())
        return QStringLiteral(".");
    return prefix + comps.join(u'\\');
}

QString abspath(const QString &p)
{
    // CPython on Windows: _getfullpathname(normpath(p)) = GetFullPathNameW (it also trims trailing dots / spaces)
    const std::wstring in = normpath(p).toStdWString();
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetFullPathNameW(in.c_str(), DWORD(std::size(buf)), buf, nullptr);
    if (n == 0 || n >= std::size(buf))
        return normpath(join(QDir::toNativeSeparators(QDir::currentPath()), p));
    return QString::fromWCharArray(buf, qsizetype(n));
}

QString normcase(const QString &p)
{
    QString s = p;
    s.replace(u'/', u'\\');
    return py::lower(s);
}

static qsizetype split_at(const QString &p)
{
    // index just after the last separator, after the drive
    const qsizetype d = drive_of(p).size();
    qsizetype i = p.size();
    while (i > d && !is_sep(p[i - 1]))
        --i;
    return i;
}

QString dirname(const QString &p)
{
    const qsizetype d = drive_of(p).size();
    const qsizetype i = split_at(p);
    QString head = p.left(i);
    // strip trailing separators unless head is all separators (after the drive)
    QString h = head.mid(d);
    bool all_sep = true;
    for (QChar c : h)
        all_sep = all_sep && is_sep(c);
    if (!all_sep) {
        while (!head.isEmpty() && is_sep(head.back()))
            head.chop(1);
    }
    return head;
}

QString basename(const QString &p) { return p.mid(split_at(p)); }

QString splitext_stem(const QString &p)
{
    // (ntpath: "\\" and "/" both separate)
    const qsizetype sep = std::max(p.lastIndexOf(u'/'), p.lastIndexOf(u'\\'));
    const qsizetype dot = p.lastIndexOf(u'.');
    if (dot > sep)
        for (qsizetype i = sep + 1; i < dot; ++i)
            if (p[i] != u'.')
                return p.left(dot);
    return p;
}

QString relpath(const QString &p, const QString &start)
{
    const QStringList a = abspath(start).split(u'\\', Qt::SkipEmptyParts);
    const QStringList b = abspath(p).split(u'\\', Qt::SkipEmptyParts);
    qsizetype i = 0;
    while (i < a.size() && i < b.size() && a[i].toLower() == b[i].toLower())
        ++i;
    QStringList rel;
    for (qsizetype k = i; k < a.size(); ++k)
        rel << QStringLiteral("..");
    for (qsizetype k = i; k < b.size(); ++k)
        rel << b[k];
    return rel.isEmpty() ? QStringLiteral(".") : rel.join(u'\\');
}

}  // namespace pypath
