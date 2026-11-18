// canals against Python, the C++ side of tools/check_canals.py.  Usage: med_canals --dump <out> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/pypath.h"
#include "map/canals.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using mapview::MapData;
using OptStr = std::optional<QString>;

static QString hb(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
template <typename T>
static QString hv(const std::vector<T> &x)
{
    return hb(QByteArray::fromRawData(reinterpret_cast<const char *>(x.data()), qsizetype(x.size() * sizeof(T))));
}
static QString b(bool x) { return x ? u"True"_s : u"False"_s; }

static const QString SAMPLE =
    u"political_decisions = {\n\tdig_kiel = {\n\t\tpicture = kiel\n\t\teffect = { enable_canal = 1 }\n\t}\n\n"
    "\tother = {\n\t\tallow = { }\n\t}\n}\n\ncountry_event = {\n\tid = 77001\n\toption = { enable_canal = 2\n"
    "\t\t300 = { set_province_flag = suez_x } }\n}\n\n\n# end\n"_s;
static const QStringList SPRITES = {u""_s, u"sprite = Kiel_Canal\nx = 3\ny = -2.5\n"_s,
                                    u"# a comment\n  SPRITE = \"Old\"  # keep\nX=1.0\n"_s, u"y = 7\n\n\n"_s,
                                    u"x = 1e3\ny = abc\nsprite = A_b\n"_s};
static const std::vector<double> NUMS = {0.0, -0.0, -0.0004, 0.0005, 12.5, 1234.56789, -3.0001, 1e-9, 100.0, 2.675};

static void texts(QStringList &out)
{
    using namespace canals;
    for (const QString &t : SPRITES) {
        const Sprite s = parse_sprite(t);
        out << u"parse %1 -> %2"_s.arg(v(t), pyform::list({L(u"sprite"_s, s.sprite), L(u"x"_s, s.x), L(u"y"_s, s.y)}));
        out << u"sprite_text "_s + v(sprite_text(t, u"Suez_Canal"_s, -12.25, 3.0));
    }
    out << u"sprite_text None "_s + v(sprite_text(std::nullopt, u"Kiel_Canal"_s, 0.0, 0.0));
    QStringList ns;
    for (double x : NUMS)
        ns << fmt_num(x);
    out << u"fmt_num "_s + v(ns);
    out << u"flag_name "_s + v(QStringList{flag_name(3, {}), flag_name(3, {u"canal_3"_s}),
                                           flag_name(3, {u"canal_3"_s, u"canal_3_2"_s}), flag_name(12, {u"CANAL_12"_s})});
    out << u"decision "_s + v(decision_block(u"dig_3"_s, 3, 1204, u"canal_3"_s));
    out << u"decision pic "_s + v(decision_block(u"dig_4"_s, 4, 99, u"c4"_s, u"kielcanal"_s));
    out << u"event "_s + v(event_block(u"88123"_s, 5, 400, u"canal_5"_s));
    const std::vector<std::pair<QString, QString>> rm = {{u"decision"_s, u"dig_kiel"_s}, {u"decision"_s, u"other"_s},
                                                         {u"decision"_s, u"nope"_s}, {u"event"_s, u"77001"_s},
                                                         {u"event"_s, u"1"_s}};
    for (const auto &[kind, name] : rm)
        out << u"remove %1 %2 %3"_s.arg(kind, name, v(remove_item(SAMPLE, kind, name)));
    for (const QString t : {u""_s, u"\n\n"_s, u"political_decisions = {\n}\n"_s, u"x = 1\n\n"_s,
                            u"political_decisions = { a = { } }"_s}) {
        out << u"add_decision %1 %2"_s.arg(v(t), v(add_decision(t, u"\tz = {\n\t}\n"_s)));
        out << u"add_event %1 %2"_s.arg(v(t), v(add_event(t, u"country_event = { }\n"_s)));
    }
}

static QString img_hash(const QImage &img)
{
    return hb(QByteArray(reinterpret_cast<const char *>(img.constBits()), qsizetype(img.sizeInBytes())));
}

static void models(const QString &work, moddata::Mod &m, const MapData &md, const std::vector<canals::Canal> &cs,
                   QStringList &out)
{
    using namespace canals;
    std::vector<std::pair<QString, QString>> af;
    for (const auto &[k, x] : actor_files(m))
        af.emplace_back(k, x);
    out << u"actors "_s + v(af);
    Models ms(m);
    out << u"models "_s + v(ms.names());
    for (const QString &name : ms.names()) {
        const OptStr p = ms.path(name);
        out << u"model %1 exists %2 path %3"_s.arg(name, b(ms.exists(name)),
                                                   v(p ? OptStr(pypath::relpath(*p, work).replace(u'\\', u'/')) : std::nullopt));
        if (!ms.exists(name))
            continue;
        const auto meshes = read_meshes(*p);
        for (const Mesh &mm : meshes)
            out << u"  mesh %1 %2 nrm %3 uv %4 tris %5 %6"_s.arg(mm.pos.size())
                       .arg(hv(mm.pos), mm.nrm ? hv(*mm.nrm) : u"None"_s, mm.uv ? hv(*mm.uv) : u"None"_s)
                       .arg(mm.tris.size())
                       .arg(hv(mm.tris));
        out << u"  texture "_s + v(texture_name(*p));
        const auto &r = ms.render(name);
        if (!r)
            out << u"  render None"_s;
        else {
            out << u"  render %1x%2 minx %3 maxz %4 w %5 h %6 img %7 kind %8"_s.arg(r->img.width())
                       .arg(r->img.height())
                       .arg(v(r->minx), v(r->maxz), v(r->w), v(r->h), img_hash(r->img), hv(r->kind.flat()));
            const auto plain = render_top(meshes, std::nullopt, 5);
            out << u"  plain %1x%2 img %3"_s.arg(plain->img.width()).arg(plain->img.height()).arg(img_hash(plain->img));
            const Samples s = fit_samples(*r);
            out << u"  samples %1 %2 %3 %4"_s.arg(s.xs.size()).arg(hv(s.xs), hv(s.zs), hv(s.wet));
            for (size_t ci = 0; ci < std::min<size_t>(4, cs.size()); ++ci) {
                const Canal &c = cs[ci];
                for (int k = 0; k < 3; ++k) {
                    const double ox = double(md.w) * (k + 1) / 7.0 + 0.37, oy = double(md.h) * (k + 2) / 9.0 + 0.61;
                    const Score sc = fit_score(md, md.sea, c.prov, {c.seas.first, c.seas.second}, s, ox, oy);
                    const QString items = pyform::list(
                        {L(u"built_other"_s, sc.built_other), L(u"built_prov"_s, sc.built_prov),
                         L(u"built_sea"_s, sc.built_sea), L(u"near"_s, sc.near_), L(u"other"_s, sc.other),
                         L(u"reaches"_s, sc.reaches), L(u"score"_s, sc.score), L(u"water_other"_s, sc.water_other),
                         L(u"water_prov"_s, sc.water_prov), L(u"water_sea"_s, sc.water_sea)});
                    out << u"  score %1 %2 %3"_s.arg(c.num).arg(k).arg(items);
                }
            }
        }
        const auto &o = ms.outline(name);
        if (!o)
            out << u"  outline None"_s;
        else
            for (const auto &[k, rings] : *o) {
                std::vector<double> pts;
                size_t n = 0;
                for (const Ring &ring : rings)
                    for (const auto &[x, y] : ring) {
                        pts.push_back(x);
                        pts.push_back(y);
                        ++n;
                    }
                out << u"  outline %1 %2 rings %3 points %4"_s.arg(k).arg(rings.size()).arg(n).arg(hv(pts));
            }
    }
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    struct M {
        QString shortn, rel;
        QStringList replace;
        bool find;
    };
    const std::vector<M> mods = {
        {u"ferrum_mare"_s, u"Ferrum_Mare 1.2.000"_s,
         {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s}, false},
        {u"vanilla"_s, u"Victoria 2"_s, {}, false},
        {u"corgi"_s, u"Victoria 2\\mod\\CoRGI_puir 18.1"_s, {}, true},
        {u"modus"_s, u"Victoria 2\\mod\\Modus Omnino Demens 1.6"_s, {}, true}};
    for (const M &mo : mods) {
        const QString root = pypath::join(work, mo.rel);
        QStringList replace = mo.replace;
        if (mo.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, mo.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
        adjacency::AdjacencyFile adj(m);
        scriptstore::ScriptStore store(m);
        QStringList out;
        if (mo.shortn == u"ferrum_mare")
            texts(out);
        const auto sp = canals::sprites(m, store);
        QStringList sps;
        for (const auto &[k, d] : sp)
            sps << L(k, d.flag, d.rel, d.sprite, d.x, d.y);
        out << u"sprites "_s + pyform::list(sps);
        qsizetype ri = 0;
        for (const auto &[k, d] : sp) {
            if (ri++ >= 6)
                break;
            out << u"  rewrite %1 %2"_s.arg(k, v(canals::sprite_text(store.text(d.rel), u"Kiel_Canal"_s, d.x + 1.5, d.y - 0.25)));
        }
        const auto ops = canals::openings(m, store);
        QStringList os;
        for (const auto &[n, list] : ops) {
            QStringList ol;
            for (const canals::Opening &o : list) {
                QStringList fl;
                for (const auto &[p, f] : o.flags)
                    fl << L(p, f);
                ol << L(o.kind, o.name, o.rel, static_cast<long long>(o.pos)).chopped(1) + u", "_s + pyform::list(fl) + u']';
            }
            os << L(n).chopped(1) + u", "_s + pyform::list(ol) + u']';
        }
        out << u"openings "_s + pyform::list(os);
        const auto cs = canals::canals(adj, m, store);
        for (const canals::Canal &c : cs) {
            QStringList rows;
            for (const auto &r : c.rows)
                rows << L(r.uid, r.a, r.b, r.type, r.through, r.data, r.comment);
            out << u"canal %1 prov %2 seas %3 name %4 rows %5 flag %6 flag_prov %7 sprite %8"_s.arg(c.num)
                       .arg(c.prov)
                       .arg(L(c.seas.first, c.seas.second), v(c.name), pyform::list(rows), v(c.flag), v(c.flag_prov),
                            v(c.sprite ? OptStr(c.sprite->rel) : std::nullopt));
            out << u"  opens %1 problems %2"_s.arg(c.opens.size()).arg(v(canals::problems(c, m)));
            for (const canals::Opening &o : c.opens)
                if (o.kind == u"decision" || o.kind == u"event")
                    out << u"  removed %1 %2"_s.arg(o.kind, hb(canals::remove_item(store.text(o.rel), o.kind, o.name).toUtf8()));
        }
        models(work, m, md, cs, out);
        QFile fh(QDir(out_dir).filePath(mo.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mo.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
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
