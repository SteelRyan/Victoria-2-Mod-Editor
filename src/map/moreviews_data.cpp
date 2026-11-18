// moreviews.py, the data half.
#include "map/moreviews_data.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "map/imgio.h"
#include "map/mapsize.h"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>

using namespace Qt::StringLiterals;
using pypath::join;

namespace moreviews {

QStringList AssetEdits::dirty() const
{
    QStringList out;
    for (const auto &kv : fresh)
        out << kv.first;
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<QStringList> AssetEdits::save(const QString &backup_root)
{
    if (fresh.isEmpty())
        return std::nullopt;
    const QString stamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    std::optional<QString> out;
    std::vector<std::pair<QString, Asset>> items(fresh.begin(), fresh.end());
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[rel, a] : items) {
        const QString dst = join(mod.root, rel.split(u'/'));
        if (fs::exists(dst)) {
            const QString bdir = join(join(backup_root, {u"assets"_s, stamp}), pypath::dirname(rel).split(u'/', Qt::SkipEmptyParts));
            fs::makedirs(bdir);
            fs::copy2(dst, join(bdir, pypath::basename(rel)));
        }
        fs::makedirs(pypath::dirname(dst));
        if (a.how == u"img")
            imgio::write_tga(dst, a.img);
        else if (a.how == u"strip")                // an icon strip: 32 bit with its transparency
            imgio::write_image(dst, a.img);
        else if (a.how == u"pic")                  // (rgba, what kind of file it is): written as that kind
            fourk::write_picture(dst, a.pic->rgba, a.pic->info);
        else if (a.how == u"dds") {                // (C++ only: Map → Draw → Colours) in the format of the file at path
            QString why;
            const QByteArray bytes = mapsize::recolor_dds(a.path, a.img, &why);
            if (bytes.isEmpty())
                throw fs::OSError(u"%1 couldn't be written: %2 %3"_s.arg(rel, a.path, why).toStdString(), 22);
            QFile f(fs::watched(dst));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size())
                throw fs::OSError(u"[Errno 13] Permission denied: '%1'"_s.arg(dst).toStdString(), 13);
        } else
            fs::copy2(a.path, dst);
        out = dst;
    }
    fresh = {};
    return out ? std::optional<QStringList>(QStringList{*out}) : std::nullopt;
}

QString safe_stem(const QString &name)
{
    static const QRegularExpression re(u"[^a-z0-9_]+"_s);
    QString s = py::lower(pypath::basename(name));
    const qsizetype dot = s.lastIndexOf(u'.');
    if (dot > 0)
        s = s.left(dot);
    s.replace(re, u"_"_s);
    while (s.startsWith(u'_'))
        s.remove(0, 1);
    while (s.endsWith(u'_'))
        s.chop(1);
    return s.isEmpty() ? u"picture"_s : s;
}

QImage fit(const QImage &img, int w, int h)
{
    // The picture scaled to fill size (w, h) and cut to it (centred).
    const QImage s = img.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return s.copy((s.width() - w) / 2, (s.height() - h) / 2, w, h);
}

// ---------------------------------------------------------------------------------------------- wars at the start
const QRegularExpression DATE_RE(uR"(^\d{1,4}\.\d{1,2}\.\d{1,2}$)"_s);

QString unq(const QString &v)
{
    return py::strip(py::strip(v), u"\""_s);
}

namespace {
bool key_is(const script::Node &c, const QString &k)
{
    return c.has_key && c.key == k;
}
QString unq_of(const script::Node &c)
{
    return c.is_block ? QString() : unq(c.value);
}
}  // namespace

War read_war(const QString &text)
{
    War w;
    script::Nodes nodes;
    try {
        nodes = script::parse(text);
    } catch (...) {
        w.simple = false;
        return w;
    }
    std::vector<const script::Node *> dated;
    for (const script::Node &n : nodes) {
        if (key_is(n, u"name"_s) && !n.is_block)
            w.name = unq(n.value);
        else if (n.has_key && !n.key.isEmpty() && DATE_RE.match(n.key).hasMatch() && n.is_block)
            dated.push_back(&n);
        else if (n.has_key && !n.key.isEmpty())
            w.simple = false;
    }
    std::vector<const script::Node *> joins, leaves;
    for (const script::Node *n : dated) {
        bool j = false;
        for (const script::Node &c : n->kids)
            if (key_is(c, u"add_attacker"_s) || key_is(c, u"add_defender"_s) || key_is(c, u"war_goal"_s))
                j = true;
        (j ? joins : leaves).push_back(n);
    }
    if (joins.size() > 1 || leaves.size() > 1)
        w.simple = false;
    if (!joins.empty()) {
        const script::Node *n = joins[0];
        w.start = n->key;
        for (const script::Node &c : n->kids) {
            if (key_is(c, u"add_attacker"_s))
                w.att << unq_of(c);
            else if (key_is(c, u"add_defender"_s))
                w.def << unq_of(c);
            else if (key_is(c, u"war_goal"_s) && c.is_block) {
                QHash<QString, QString> f;
                for (const script::Node &x : c.kids)
                    if (x.has_key && !x.key.isEmpty() && !x.is_block)
                        f.insert(x.key, unq(x.value));
                w.goals.emplace_back(f.value(u"casus_belli"_s), f.value(u"actor"_s), f.value(u"receiver"_s));
                for (auto it = f.cbegin(); it != f.cend(); ++it)
                    if (it.key() != u"casus_belli" && it.key() != u"actor" && it.key() != u"receiver")
                        w.simple = false;
            } else if (c.has_key && !c.key.isEmpty())
                w.simple = false;
        }
    }
    if (!leaves.empty()) {
        const script::Node *n = leaves[0];
        w.end = n->key;
        for (const script::Node &c : n->kids)
            if (c.has_key && !c.key.isEmpty() && c.key != u"rem_attacker" && c.key != u"rem_defender")
                w.simple = false;
    }
    return w;
}

QString war_text(const War &w)
{
    QStringList out{u"name = \"%1\""_s.arg(w.name), QString(), w.start + u" = {"_s};
    for (const QString &t : w.att)
        out << u"\tadd_attacker = "_s + t;
    for (const QString &t : w.def)
        out << u"\tadd_defender = "_s + t;
    for (const auto &[cb, a, r] : w.goals)
        out << QString() << u"\twar_goal = {"_s << u"\t\tcasus_belli = "_s + cb << u"\t\tactor = "_s + a
            << u"\t\treceiver = "_s + r << u"\t}"_s;
    out << u"}"_s;
    if (!w.end.isEmpty()) {
        out << QString() << w.end + u" = {"_s;
        for (const QString &t : w.att)
            out << u"\trem_attacker = "_s + t;
        for (const QString &t : w.def)
            out << u"\trem_defender = "_s + t;
        out << u"}"_s;
    }
    return out.join(u'\n') + u'\n';
}

}  // namespace moreviews
