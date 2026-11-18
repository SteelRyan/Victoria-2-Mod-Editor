// core/fs and core/ordered_map against Python: the C++ half. tools/check_fs.py runs Python's own calls on the same
// files and compares.  Usage: med_fs <src file> <dst file> <folder to list> <out.txt>
#include "nopopup.h"
#include "core/fs.h"
#include "core/ordered_map.h"
#include "core/pypath.h"

#include <QFile>
#include <QString>
#include <cstdio>
#include <functional>

int main(int argc, char *argv[])
{
    if (argc < 5)
        return 2;
    const QString src = QString::fromLocal8Bit(argv[1]), dst = QString::fromLocal8Bit(argv[2]);
    const QString folder = QString::fromLocal8Bit(argv[3]);
    QString out;
    fs::copy2(src, dst);
    out += QStringLiteral("copied\n");
    out += QStringLiteral("listdir\t") + fs::listdir(folder).join(u'|') + u'\n';
    out += QStringLiteral("mtime\t") + QString::number(fs::getmtime(dst), 'f', 6) + u'\n';
    // dict order: put, overwrite, delete, put again
    OrderedMap<QString, int> d;
    d.insert(QStringLiteral("b"), 1);
    d.insert(QStringLiteral("a"), 2);
    d.insert(QStringLiteral("c"), 3);
    d.insert(QStringLiteral("b"), 4);
    d.remove(QStringLiteral("a"));
    d.insert(QStringLiteral("a"), 5);
    d[QStringLiteral("d")] += 6;
    QString order;
    for (const auto &[k, v] : d)
        order += k + u'=' + QString::number(v) + u' ';
    out += QStringLiteral("dict\t") + order.trimmed() + u'\n';
    OrderedMap<QString, int> e;
    e.insert(QStringLiteral("a"), 5);
    e.insert(QStringLiteral("d"), 6);
    e.insert(QStringLiteral("c"), 3);
    e.insert(QStringLiteral("b"), 4);
    out += QStringLiteral("dict_eq\t") + (d == e ? QStringLiteral("True") : QStringLiteral("False")) + u'\n';
    // the text of a failed call (str(OSError) in Python), on paths inside the check's own folder
    const QString dir = pypath::dirname(dst);
    auto err = [&out](const char *tag, const std::function<void()> &f) {
        try {
            f();
            out += QString::fromLatin1(tag) + QStringLiteral("\tno error\n");
        } catch (const fs::OSError &e) {
            out += QString::fromLatin1(tag) + u'\t' + QString::fromStdString(e.what()) + u'\n';
        }
    };
    err("err_makedirs", [&] { fs::makedirs(pypath::join(pypath::join(dst, QStringLiteral("sub")), QStringLiteral("deeper"))); });
    err("err_makedirs_exists", [&] { fs::makedirs(dir, false); });
    err("err_listdir", [&] { fs::listdir(pypath::join(dir, QStringLiteral("it's missing"))); });
    err("err_remove", [&] { fs::remove(pypath::join(dir, QStringLiteral("it's missing.txt"))); });
    err("err_replace", [&] { fs::replace(pypath::join(dir, QStringLiteral("it's missing.txt")),
                                         pypath::join(dir, QStringLiteral("other.txt"))); });
    QFile f(QString::fromLocal8Bit(argv[4]));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write(out.toUtf8());
    return 0;
}
