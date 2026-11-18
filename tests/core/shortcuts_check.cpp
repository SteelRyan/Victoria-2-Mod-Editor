// shortcutfacts + shortcutuse against Python, the C++ side of tools/check_shortcuts.py.
// Usage: med_shortcuts --dump <out folder> <buttons.json (written by the Python side)>
#include "nopopup.h"
#include "core/shortcutfacts.h"
#include "core/shortcutuse.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace shortcutuse;
using pyform::L;
using pyform::v;

template <typename M> static QString items_v(const M &m)
{
    QStringList p;
    for (const auto &[a, b] : m)
        p << L(a, b);
    return pyform::list(p);
}
template <typename M> static QString triples_v(const M &m)
{
    QStringList p;
    for (const auto &[a, b] : m)
        p << L(a, b.first, b.second);
    return pyform::list(p);
}

static int dump(const QString &out_dir, const QString &json)
{
    QDir().mkpath(out_dir);
    QStringList common = {u"tested "_s + v(shortcutfacts::TESTED), u"word "_s + items_v(shortcutfacts::WORD()),
                          u"facts %1"_s.arg(shortcutfacts::FACTS().size())};
    for (const auto &[k, f] : shortcutfacts::FACTS())
        common << u"fact %1 %2"_s.arg(v(k), L(f.cls, f.text));
    common << u"CLASS_NAME "_s + items_v(CLASS_NAME) << u"ROWS "_s + items_v(ROWS) << u"WHAT "_s + items_v(WHAT)
           << u"SORT_WORD "_s + items_v(SORT_WORD);
    common << u"WINDOWS "_s + triples_v(WINDOWS) << u"PLACES "_s + triples_v(PLACES);
    common << u"LEFTOVER "_s + v(LEFTOVER);
    for (const QString &s : {u""_s, u"abc"_s, u"fooBarBaz"_s, u"close_button"_s, u"my_btn_x"_s, u"CheckBox"_s, u"button"_s,
                             u"ABC"_s, u"aB_cD"_s, u" x_ "_s})
        common << u"pretty %1 %2"_s.arg(v(s), v(_pretty(s)));
    QStringList tops;
    for (const auto &kv : WINDOWS)
        if (tops.size() < 5)
            tops << kv.first;
    qsizetype nr = 0;
    for (const auto &kv : ROWS)
        if (nr++ < 5)
            tops << kv.first;
    tops << u""_s << u"someWindow"_s << u"my_entry_x"_s;
    for (const QString &top : tops)
        for (const QString &rel : {u""_s, u"interface/tutorial.gui"_s, u"interface/X.gui"_s}) {
            const auto t = window_title(top, rel);
            common << u"title %1 %2 %3"_s.arg(v(top), v(rel), L(t.first, t.second));
        }
    QFile jf(json);
    if (!jf.open(QIODevice::ReadOnly))
        return 4;
    const QJsonObject lists = QJsonDocument::fromJson(jf.readAll()).object();
    for (const QString &shortn : {u"ferrum_mare"_s, u"vanilla"_s, u"corgi"_s, u"modus"_s}) {
        const QJsonArray buttons = lists.value(shortn).toArray();
        QStringList out = common;
        out << u"buttons %1"_s.arg(buttons.size());
        for (const QJsonValue &e : buttons) {
            const QJsonArray a = e.toArray();
            Button b;
            b.rel = a[0].toString();
            b.name = a[1].toString();
            for (const QJsonValue &p : a[2].toArray())
                b.path << p.toString();
            b.nth = a[3].toInteger();
            const QString says = a[4].toString();
            const auto [c, why, does] = classify(b, says);
            out << u"b %1 %2 %3 %4 %5 | %6 | %7 | %8"_s.arg(b.rel, v(b.name), v(b.path), QString::number(b.nth), v(says),
                                                            L(c, why, does), v(is_row(b)), v(what(b)));
        }
        QFile f(QDir(out_dir).filePath(shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
