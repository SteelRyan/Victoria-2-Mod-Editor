// prodtree (+ py::hypot) against Python, the C++ side of tools/check_prodtree.py.
// Usage: med_prodtree --dump <out folder> <cases.json (written by the Python side)>
#include "nopopup.h"
#include "core/prodtree.h"
#include "core/pynum.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstdlib>

using namespace Qt::StringLiterals;
using namespace prodtree;
using pyform::L;
using pyform::v;

static Cid cid(const QJsonValue &j)
{
    const QJsonArray a = j.toArray();
    return {a[0].toString(), a[1].toString(), a[2].toInteger()};
}
static std::optional<double> optd(const QJsonValue &j) { return j.isNull() ? std::nullopt : std::optional<double>(j.toDouble()); }
static std::optional<QString> opts(const QJsonValue &j) { return j.isNull() ? std::nullopt : std::optional<QString>(j.toString()); }
static Rgb rgb(const QJsonValue &j)
{
    const QJsonArray a = j.toArray();
    return {a[0].toInt(), a[1].toInt(), a[2].toInt()};
}
static Edge edge(const QJsonValue &j)
{
    const QJsonArray a = j.toArray();
    return {cid(a[0]), a[1].toDouble(), cid(a[2]), a[3].toDouble(), {cid(a[4]), a[5].toString()}, a[6].toInteger(),
            a[7].toString()};
}
static Kids kids(const QJsonValue &j)
{
    Kids out;
    for (const QJsonValue &e : j.toArray()) {
        const QJsonArray p = e.toArray();
        std::vector<Cid> lst;
        for (const QJsonValue &x : p[1].toArray())
            lst.push_back(cid(x));
        out.insert(cid(p[0]), lst);
    }
    return out;
}
static std::vector<RowItem> row(const QJsonValue &j)
{
    std::vector<RowItem> out;
    for (const QJsonValue &e : j.toArray()) {
        const QJsonArray a = e.toArray();
        out.push_back({cid(a[0]), a[1].toInteger(), a[2].toString(), opts(a[3])});
    }
    return out;
}

static QString cid_v(const Cid &c) { return L(c.kind, c.key, c.n); }
static QString ocid_v(const OptCid &c) { return c ? cid_v(*c) : u"None"_s; }
static QString rgb_v(const Rgb &r) { return L(std::get<0>(r), std::get<1>(r), std::get<2>(r)); }

static QStringList scene_lines(const Scene &sc)
{
    QStringList out;
    for (const auto &[k, r] : sc.rects)
        out << u"rect %1 %2"_s.arg(cid_v(k), L(r[0], r[1], r[2], r[3]));
    for (const auto &[ni, lst] : sc.pieces)
        for (const Piece &p : lst)
            out << u"pieces %1 %2"_s.arg(QString::number(ni), pyform::list({v(p.kind), v(p.x1), v(p.y1), v(p.x2), v(p.y2),
                                                                             v(p.style), ocid_v(p.tgt)}));
    for (const auto &[ni, lst] : sc.dots)
        for (const auto &[x, y] : lst)
            out << u"dots %1 %2"_s.arg(QString::number(ni), L(x, y));
    for (const auto &[ni, lst] : sc.arrows)
        for (const Arrow &a : lst) {
            const QString dir = std::holds_alternative<QString>(a.dir) ? v(std::get<QString>(a.dir)) : v(std::get<double>(a.dir));
            out << u"arrows %1 %2"_s.arg(QString::number(ni), pyform::list({v(a.x), v(a.y), dir, v(a.style), ocid_v(a.tgt)}));
        }
    for (const auto &[ni, lst] : sc.labels)
        for (const auto &[x, y] : lst)
            out << u"labels %1 %2"_s.arg(QString::number(ni), L(x, y));
    for (const auto &[ni, lst] : sc.marks)
        for (const auto &[x, y] : lst)
            out << u"marks %1 %2"_s.arg(QString::number(ni), L(x, y));
    for (const Text &t : sc.texts)
        out << u"text "_s + pyform::list({v(t.x), v(t.y), v(t.w), v(t.text), rgb_v(t.rgb), v(t.px), v(t.align)});
    for (const auto &g : sc.guides)
        out << u"guide "_s + L(g[0], g[1], g[2], g[3]);
    for (const auto &[a, b, c, d, r] : sc.wedges)
        out << u"wedge "_s + pyform::list({v(a), v(b), v(c), v(d), rgb_v(r)});
    for (const auto &[x, y, t] : sc.pills)
        out << u"pill "_s + L(x, y, t);
    for (const Net &n : sc.nets) {
        QStringList src, tgt;
        for (const auto &[a, b] : n.src)
            src << pyform::list({cid_v(a), v(b)});
        for (const auto &[a, b, s] : n.tgt)
            tgt << pyform::list({ocid_v(a), v(b), u"None"_s, v(s)});
        out << u"net %1 %2 %3 %4 %5 %6"_s.arg(v(n.id), pyform::list(src), pyform::list(tgt), v(n.good), rgb_v(n.rgb), v(n.what));
    }
    return out;
}

static int dump(const QString &out_dir, const QString &json)
{
    QDir().mkpath(out_dir);
    QFile jf(json);
    if (!jf.open(QIODevice::ReadOnly))
        return 4;
    const QJsonObject all = QJsonDocument::fromJson(jf.readAll()).object();
    QStringList lines;
    for (const QJsonValue &p : all.value(u"hypot"_s).toArray()) {
        const QJsonArray a = p.toArray();
        const double x = std::strtod(a[0].toString().toLatin1().constData(), nullptr);
        const double y = std::strtod(a[1].toString().toLatin1().constData(), nullptr);
        lines << u"hypot %1 %2 %3"_s.arg(v(x), v(y), v(py::hypot(x, y)));
    }
    long long i = 0;
    for (const QJsonValue &cv : all.value(u"cases"_s).toArray()) {
        const QJsonObject c = cv.toObject();
        Specs spec;
        for (const QJsonValue &s : c.value(u"spec"_s).toArray()) {
            const QJsonArray a = s.toArray();
            spec.insert(cid(a[0]), {a[1].toDouble(), a[2].toDouble(), optd(a[3]), optd(a[4]), optd(a[5])});
        }
        Scene sc;
        for (const QJsonValue &n : c.value(u"nets"_s).toArray()) {
            const QJsonArray a = n.toArray();
            sc.net(a[0].toString(), opts(a[1]), rgb(a[2]), std::nullopt, opts(a[4]));
        }
        const QString kind = c.value(u"kind"_s).toString();
        QString err;
        try {
            if (kind == u"explorer") {
                std::vector<Edge> edges;
                for (const QJsonValue &e : c.value(u"edges"_s).toArray())
                    edges.push_back(edge(e));
                std::optional<BoxItem> box;
                if (!c.value(u"box"_s).isNull()) {
                    const QJsonArray b = c.value(u"box"_s).toArray();
                    box = BoxItem{cid(b[0]), b[1].toInteger()};
                }
                std::vector<Group> groups;
                for (const QJsonValue &gv : c.value(u"groups"_s).toArray()) {
                    const QJsonArray g = gv.toArray();
                    Group grp{g[0].toString(), g[1].toInteger(), g[2].toBool(), {}};
                    for (const QJsonValue &iv : g[3].toArray()) {
                        const QJsonArray it = iv.toArray();
                        GroupItem gi{cid(it[0]), it[1].toString(), optd(it[2]), it[3].toString(), opts(it[4]), std::nullopt};
                        if (!it[5].isNull()) {
                            const QJsonArray t = it[5].toArray();
                            Unfolded u{kids(t[0]), {}};
                            for (const QJsonValue &e : t[1].toArray())
                                u.edges.push_back(edge(e));
                            gi.tree = u;
                        }
                        grp.items.push_back(gi);
                    }
                    groups.push_back(grp);
                }
                const QJsonArray t = c.value(u"titles"_s).toArray();
                explorer_scene(sc, spec, cid(c.value(u"centre"_s)), kids(c.value(u"up"_s)), edges, row(c.value(u"top"_s)),
                               row(c.value(u"bottom"_s)), box, groups,
                               {t[0].toString(), t[1].toString(), t[2].toString(), t[3].toString()});
            } else {
                std::vector<Cid> cards;
                for (const QJsonValue &x : c.value(u"cards"_s).toArray())
                    cards.push_back(cid(x));
                std::vector<Sector> sectors;
                for (const QJsonValue &sv : c.value(u"sectors"_s).toArray()) {
                    const QJsonArray s = sv.toArray();
                    Sector sec{s[0].toString(), rgb(s[1]), {}};
                    for (const QJsonValue &iv : s[2].toArray()) {
                        const QJsonArray it = iv.toArray();
                        SectorItem si{cid(it[0]), it[1].toInteger(), {}};
                        for (const QJsonValue &m : it[2].toArray())
                            si.makers.emplace_back(cid(m.toArray()[0]), m.toArray()[1].toInteger());
                        sec.items.push_back(si);
                    }
                    sectors.push_back(sec);
                }
                hub_scene(sc, spec, cards, sectors, c.value(u"name"_s).toString());
            }
        } catch (const std::exception &e) {
            err = u"Error("_s + QString::fromUtf8(e.what()) + u")"_s;
        }
        lines << u"case %1 %2 %3 %4"_s.arg(QString::number(i++), kind, err, QString::number(sc.rects.size()));
        lines += scene_lines(sc);
        const auto b = sc.bounds();
        lines << u"bounds "_s + L(b[0], b[1], b[2], b[3]);
    }
    QFile f(QDir(out_dir).filePath(u"g2.txt"_s));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write((lines.join(u'\n') + u'\n').toUtf8());
    std::printf("g2 %lld lines\n", static_cast<long long>(lines.size()));
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
