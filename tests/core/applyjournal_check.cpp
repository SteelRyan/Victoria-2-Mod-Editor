// applyjournal against Python, the C++ side of tools/check_applyjournal.py.  Usage: med_applyjournal --run <work folder>
#include "nopopup.h"
#include "core/applyjournal.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QCoreApplication>
#include <QFile>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace applyjournal;
using pypath::join;

static QString b(bool x) { return x ? u"True"_s : u"False"_s; }
static QString strs(const QStringList &l)
{
    QStringList p;
    for (const QString &x : l)
        p << py::repr_str(x);
    return u'[' + p.join(u", "_s) + u']';
}
static QString done_txt(const std::pair<long long, QStringList> &r)   // "%s" % ((n, [...]),) -> (n, [...])
{
    return u"(%1, %2)"_s.arg(r.first).arg(strs(r.second));
}
static QString label(const std::optional<ApplyJournal::Entry> &e)
{
    if (!e)
        return u"None"_s;
    const pyjson::Value *l = e->second.get(u"label"_s);
    return l ? l->str() : u"None"_s;
}

static int run(const QString &w)
{
    const QString mod = join(w, u"mod"_s), base = join(w, u"base"_s), backups = join(w, u"backups"_s);
    ApplyJournal j(backups, mod, base);
    QStringList out;
    j.record(u"Apply one"_s, u"1836.1.1"_s, [&](Recorder &rec) {
        script::write_text(join(mod, {u"common"_s, u"a.txt"_s}), u"a = 2\n"_s);
        fs::makedirs(join(mod, {u"common"_s, u"new"_s}));
        script::write_text(join(mod, {u"common"_s, u"new"_s, u"b.txt"_s}), u"b\n"_s);
        fs::copy2(join(w, {u"src"_s, u"c.txt"_s}), join(mod, {u"map"_s, u"c.txt"_s}));
        script::write_text(join(mod, {u"map"_s, u"tmp.txt"_s}), u"replaced in\n"_s);
        fs::replace(join(mod, {u"map"_s, u"tmp.txt"_s}), join(mod, {u"map"_s, u"old.txt"_s}));
        fs::remove(join(mod, {u"events"_s, u"e.txt"_s}));
        script::write_text(join(base, {u"common"_s, u"f.txt"_s}), u"game file, edited\n"_s);
        fs::makedirs(join(backups, u"x"_s));
        script::write_text(join(backups, {u"x"_s, u"note.txt"_s}), u"a backup\n"_s);
        {
            QFile f(join(mod, {u"common"_s, u"keep.txt"_s}));      // (unwatched: untracked)
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                f.write("changed behind its back\n");
        }
        script::write_text(join(mod, {u"common"_s, u"a.txt"_s}), u"a = 3\n"_s);   // (a second write: once recorded)
        const std::vector<std::pair<QString, std::function<void()>>> tries = {
            {u"mod folder"_s, [&] { script::write_text(join(mod, {u"mod"_s, u"x"_s, u"y.txt"_s}), u"no"_s); }},
            {u"base remove"_s, [&] { fs::remove(join(base, {u"common"_s, u"g.txt"_s})); }}};
        for (const auto &[what, fn] : tries) {
            try {
                fn();
                out << u"refused %1: no"_s.arg(what);
            } catch (const WriteRefused &e) {
                out << u"refused %1: %2"_s.arg(what, QString::fromUtf8(e.what()).replace(w, u"<w>"_s));
            }
        }
        std::vector<std::pair<QString, pyjson::Value>> items(rec.files.begin(), rec.files.end());
        std::sort(items.begin(), items.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
        QStringList fs_;
        for (const auto &[rel, v] : items)
            fs_ << u"(%1, {'before': %2})"_s.arg(py::repr_str(rel), b(v.get(u"before"_s)->truthy()));
        out << u"files ["_s + fs_.join(u", "_s) + u']';
    });
    {
        const auto es = j.entries();
        out << u"manifest1 "_s + b(!es.empty());
    }
    try {
        j.record(u"Apply two"_s, QString(), [&](Recorder &) {
            script::write_text(join(mod, {u"common"_s, u"a.txt"_s}), u"a = 4\n"_s);
            throw std::runtime_error("stopped half way");
        });
    } catch (const std::runtime_error &e) {
        out << u"raised "_s + QString::fromUtf8(e.what());
    }
    j.record(u"Apply three"_s, QString(), [&](Recorder &) {
        script::write_text(join(mod, {u"common"_s, u"a.txt"_s}), u"a = 4\n"_s);   // (the same text: nothing changes)
    });
    {
        bool three = false;
        for (const auto &e : j.entries())
            three = three || label(e) == u"Apply three";
        out << u"manifest3 "_s + (three ? u"(a manifest)"_s : u"None"_s);
    }
    auto es = j.entries();
    QStringList labels;
    for (const auto &e : es)
        labels << label(e);
    out << u"entries "_s + strs(labels);
    auto u = j.undoable();
    out << u"undoable %1 / redoable %2"_s.arg(label(u), label(j.redoable()));
    out << u"describe "_s + describe(u->second);
    out << u"undo "_s + done_txt(j.undo(*u));
    script::write_text(join(mod, {u"common"_s, u"a.txt"_s}), u"a = 9 (after the undo)\n"_s);
    auto r = j.redoable();
    out << u"redoable "_s + label(r);
    out << u"redo "_s + done_txt(j.redo(*r));
    out << u"undoable now "_s + label(j.undoable());
    auto first = es[0];
    out << u"undo first "_s + done_txt(j.undo(first));
    out << u"undo first again "_s + done_txt(j.undo(first));
    out << u"redo first "_s + done_txt(j.redo(first));
    const QByteArray o = (out.join(u'\n') + u'\n').toUtf8();
    std::fwrite(o.constData(), 1, size_t(o.size()), stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 3 && a[1] == u"--run")
            return run(a[2]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
