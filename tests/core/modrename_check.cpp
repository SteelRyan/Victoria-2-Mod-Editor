// modrename (C++ only): a mods folder made in <scratch>: the mod, its 4K submod (dependencies naming it), another mod.
// Usage: med_modrename_check <scratch folder>
#include "nopopup.h"
#include "core/modrename.h"
#include "core/script.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cstdio>

using namespace Qt::StringLiterals;

static int fails = 0;
static void ok(bool c, const QString &what)
{
    std::printf("%s %s\n", c ? "ok  " : "FAIL", qUtf8Printable(what));
    if (!c)
        ++fails;
}
static void put(const QString &p, const QByteArray &b)
{
    QDir().mkpath(QFileInfo(p).path());
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write(b);
}
static QByteArray get(const QString &p)
{
    QFile f(p);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 2)
        return 2;
    const QString root = QDir(QString::fromLocal8Bit(argv[1])).filePath(u"modrename_mods"_s);
    QDir(root).removeRecursively();
    const QString mods = root + u"/mod"_s;
    for (const QString &d : {u"Old Mod"_s, u"Old Mod 4K"_s, u"Other"_s}) {
        put(mods + u"/"_s + d + u"/map/definition.csv"_s, "province;red;green;blue;x;x\r\n");
        QDir().mkpath(mods + u"/"_s + d + u"/common"_s);
        QDir().mkpath(mods + u"/"_s + d + u"/history/pops"_s);
    }
    put(mods + u"/Old Mod.mod"_s, "name = \"Old Mod\"\r\npath = \"mod/Old Mod\"\r\nuser_dir = \"oldmod\"\r\n");
    put(mods + u"/Old Mod 4K.mod"_s, "name = \"Old Mod 4K\"\r\npath = \"mod/Old Mod 4K\"\r\n"
                                     "dependencies = { \"Old Mod\" }\r\nuser_dir = \"oldmod\"\r\n");
    put(mods + u"/Other.mod"_s, "name = \"Other\"\r\npath = \"mod/Other\"\r\ndependencies = { \"Old Mod 4K\" }\r\n");

    ok(modrename::check_name(u"  "_s) != QString(), u"empty name refused"_s);
    ok(modrename::check_name(u"A \"quoted\" one"_s) != QString(), u"a quote refused"_s);
    ok(modrename::check_name(u"日本"_s) != QString(), u"letters the game can't show refused"_s);
    ok(modrename::check_name(u"Révolution — 1848 € édition"_s).isEmpty(), u"Western letters, dash, euro sign allowed"_s);
    ok(modrename::with_name(u"path = \"x\"\n"_s, u"N"_s) == u"name = \"N\"\npath = \"x\"\n"_s, u"no name line: added"_s);
    ok(modrename::with_name(u"name=Bare\r\n"_s, u"New Name"_s) == u"name=\"New Name\"\r\n"_s, u"bare name replaced"_s);

    auto r = modrename::rename(mods + u"/Old Mod"_s, u"Old Mod"_s, u"  Brand New  "_s);
    ok(r.ok && r.error.isEmpty(), u"renamed: "_s + r.error);
    ok(get(mods + u"/Old Mod.mod"_s) == "name = \"Brand New\"\r\npath = \"mod/Old Mod\"\r\nuser_dir = \"oldmod\"\r\n",
       u"own .mod: only the name changed (CRLF kept)"_s);
    ok(get(mods + u"/Old Mod 4K.mod"_s) == "name = \"Old Mod 4K\"\r\npath = \"mod/Old Mod 4K\"\r\n"
                                           "dependencies = { \"Brand New\" }\r\nuser_dir = \"oldmod\"\r\n",
       u"submod: its dependency now names the new name, its own name unchanged"_s);
    ok(r.dependents == QStringList{u"Old Mod 4K.mod"_s}, u"dependents reported: "_s + r.dependents.join(u","_s));
    ok(get(mods + u"/Other.mod"_s).contains("{ \"Old Mod 4K\" }"), u"a dependency on ANOTHER name untouched"_s);
    r = modrename::rename(root + u"/nowhere"_s, u"X"_s, u"Y"_s);
    ok(!r.ok && !r.error.isEmpty(), u"folder without .mod: refused — "_s + r.error);
    r = modrename::rename(mods + u"/Old Mod"_s, u"Brand New"_s, u"bad \"name"_s);
    ok(!r.ok && get(mods + u"/Old Mod.mod"_s).contains("Brand New"), u"bad name: refused, file unchanged"_s);
    QDir(root).removeRecursively();
    std::printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
