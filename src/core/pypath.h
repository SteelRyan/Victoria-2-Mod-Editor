// Python's os.path on Windows (ntpath), where the spelling of a path matters: it reaches messages, settings keys,
// the stores' keys and the files' names. Only what the editor uses.
#pragma once
#include <QString>
#include <QStringList>
#include <initializer_list>

namespace pypath {

inline bool is_sep(QChar c) { return c == u'\\' || c == u'/'; }

// the drive part: "C:" or a UNC "\\server\share" (ntpath.splitdrive)
QString drive_of(const QString &p);

// ntpath.isabs (Python 3.13+: a drive AND a root, or a UNC path — "\x" alone is NOT absolute any more)
inline bool isabs(const QString &p)
{
    if (p.size() >= 2 && is_sep(p[0]) && is_sep(p[1]))
        return true;
    return p.size() >= 3 && p[1] == u':' && is_sep(p[2]);
}

// os.path.join(a, *parts)
QString join(const QString &a, const QStringList &parts);
inline QString join(const QString &a, std::initializer_list<QString> parts) { return join(a, QStringList(parts)); }
inline QString join(const QString &a, const QString &b) { return join(a, QStringList{b}); }

QString normpath(const QString &p);     // os.path.normpath
QString abspath(const QString &p);      // os.path.abspath (relative: against the current folder)
QString normcase(const QString &p);     // os.path.normcase: lower case, '/' -> '\'
QString dirname(const QString &p);      // os.path.dirname
QString basename(const QString &p);     // os.path.basename
QString splitext_stem(const QString &p);   // os.path.splitext(p)[0] (leading dots are no extension: "..txt" stays)
QString relpath(const QString &p, const QString &start);   // os.path.relpath

}  // namespace pypath
