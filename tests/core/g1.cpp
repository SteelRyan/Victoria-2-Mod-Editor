// G1, the C++ side (PLAN.md §7): fingerprint every script file of a mod with the C++ parser, in exactly the form of
// tools/golden_g1.py, so the two lists can be compared line by line.
// Usage: med_g1 <mod folder> <out.tsv> [--skip-mod]   (--skip-mod: leave out <mod folder>\mod, as for the game)
#include "nopopup.h"
#include "core/script.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>
#include <algorithm>
#include <cstdio>

static void records(const script::Nodes &nodes, int depth, QString &out, qsizetype &count)
{
    for (const script::Node &n : nodes) {
        if (count)
            out += QChar(0x1e);
        ++count;
        out += QString::number(depth);
        out += QChar(0x1f);
        out += n.has_key ? u'K' + n.key : QStringLiteral("-");
        out += QChar(0x1f);
        out += n.is_block ? QStringLiteral("{") : u'V' + n.value;
        out += QChar(0x1f);
        out += QString::number(n.kstart);
        out += QChar(0x1f);
        out += QString::number(n.vstart);
        out += QChar(0x1f);
        out += QString::number(n.vend);
        if (n.is_block)
            records(n.kids, depth + 1, out, count);
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: med_g1 <mod folder> <out.tsv> [--skip-mod]\n");
        return 2;
    }
    const QString root = QDir(args[1]).absolutePath();
    const QString skip = args.contains(QStringLiteral("--skip-mod")) ? QDir(root).filePath(QStringLiteral("mod")) : QString();
    QElapsedTimer t;
    t.start();
    QStringList rows;
    qint64 nodes_all = 0;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!skip.isEmpty() && path.startsWith(skip + u'/'))
            continue;
        const QString low = path.toLower();
        if (!(low.endsWith(u".txt") || low.endsWith(u".gfx") || low.endsWith(u".gui")))
            continue;
        const QString text = script::read_text(path);
        QString out;
        qsizetype count = 0;
        records(script::parse(text), 0, out, count);
        nodes_all += count;
        const QByteArray sha = QCryptographicHash::hash(out.toLatin1(), QCryptographicHash::Sha1).toHex();
        rows << QStringLiteral("%1\t%2\t%3\t%4").arg(path.mid(root.size() + 1), QString::fromLatin1(sha))
                    .arg(count).arg(text.size());
    }
    std::sort(rows.begin(), rows.end());
    QFile f(args[2]);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write((rows.join(u'\n') + u'\n').toUtf8());
    std::printf("%lld files, %lld nodes, %.1f s\n", (long long)rows.size(), (long long)nodes_all, t.elapsed() / 1000.0);
    return 0;
}
