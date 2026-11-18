// newprov's data half against Python, the C++ side of tools/check_newprov.py.
// Usage: med_newprov --dump <out> <work>  |  --run <copy> <backups> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/mapfiles.h"
#include "core/pypath.h"
#include "core/script.h"
#include "map/newprov.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using mapview::MapData;
using namespace newprov;

static QString h(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

static QString rec_txt(const NewProv &rec)
{
    QString info = u"[]"_s;
    if (!rec.sea)                                  // (sorted keys: controller, cores, life_rating, owner, trade_goods)
        info = pyform::list({pyform::L(u"controller"_s, rec.info.controller), pyform::L(u"cores"_s, rec.info.cores),
                             pyform::L(u"life_rating"_s, rec.info.life_rating), pyform::L(u"owner"_s, rec.info.owner),
                             pyform::L(u"trade_goods"_s, rec.info.trade_goods)});
    const QString text = rec.hist_text ? h(*rec.hist_text) : u"None"_s;
    const QString first = rec.hist_text && !rec.hist_text->isEmpty() ? v(py::splitlines(*rec.hist_text).value(0)) : u"None"_s;
    return u"name %1 rgb %2 sea %3 tmpl %4 rel %5 text %6 %7 info %8 done %9 "_s.arg(
               v(rec.name), v(rec.rgb), v(rec.sea), v(rec.tmpl), v(rec.hist_rel), text, first, info, v(rec.done_version)) +
           v(rec.done_area);
}

static document::Document *doc_of(moddata::Mod &m)
{
    QString sc;
    for (const QString &s : m.pop_scenarios())
        if (script::parse_date(s)) {
            sc = s;
            break;
        }
    return new document::Document(m, sc);
}

static moddata::Mod open_mod(const QString &work, const QString &shortn, const QString &rel, QStringList replace, bool find)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QString root = pypath::join(work, rel);
    if (find) {
        auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
        for (const auto &e : list)
            if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                replace = e.replace;
    }
    return moddata::Mod(root, shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
}

static const QStringList NAMES = {u"Neu Stadt"_s, u"  a/b\\c:d*e?f\"g<h>i|j  "_s, u"Ende..."_s, u"..."_s, u""_s,
                                  u"Łódź"_s, u"Saint-Étienne"_s, QString::fromUtf8("x\xF0\x9F\x98\x80y"),
                                  u"C;D"_s};

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
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
    for (const M &mm : mods) {
        moddata::Mod m = open_mod(work, mm.shortn, mm.rel, mm.replace, mm.find);
        m.load();
        MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
        std::unique_ptr<document::Document> d(doc_of(m));
        ProvinceDefs defs(m, md, *d->hist);
        const auto spare = defs.spare_rows();
        QStringList out = {u"next %1 spare %2 %3"_s.arg(defs.next_id()).arg(spare.size()).arg(
            v(std::vector<moddata::Rgb>(spare.begin(), spare.begin() + std::min<size_t>(5, spare.size()))))};
        long long nc = 0;
        for (bool b : defs.file_colours())
            nc += b;
        out << u"file colours %1"_s.arg(nc);
        const auto [c, where] = defs.pick_colour();
        out << u"colour %1 %2"_s.arg(v(c), where);
        long long n = 0, changed = 0;
        QString allh;
        for (const auto &[pid, files] : d->hist->files)
            for (const auto &hf : files) {
                const QString t = history_without_buildings(hf->text);
                ++n;
                allh += h(t);
                if (t != hf->text) {
                    ++changed;
                    if (changed <= 6)
                        out << u"  cut %1 %2 %3 -> %4 %5"_s.arg(pid).arg(v(hf->rel)).arg(hf->text.size()).arg(t.size()).arg(h(t));
                }
            }
        out << u"history %1 files, %2 changed, %3"_s.arg(n).arg(changed).arg(h(allh));
        QStringList sn;
        for (const QString &x : NAMES)
            sn << safe_file_name(x);
        out << u"names "_s + v(sn);
        long long top = 0;
        for (const auto &kv : m.provinces)
            top = std::max(top, kv.first);
        QStringList near_;
        for (long long p : {0LL, 1LL, 50LL, 777LL, top, top + 40})
            near_ << _history_folder_near(*d->hist, p);
        out << u"near "_s + v(near_);
        std::vector<long long> land;
        for (const auto &[p, pr] : m.provinces)
            if (!pr.sea)
                land.push_back(p);
        std::vector<long long> tpls(land.begin(), land.begin() + 3);
        tpls.insert(tpls.end(), land.begin() + land.size() / 2, land.begin() + land.size() / 2 + 2);
        tpls.insert(tpls.end(), land.end() - 2, land.end());
        auto plan_line = [&](const QString &head, const Plan &pl) {
            out << u"%1%2 %3 rid %4 cont %5 clim %6 want %7"_s.arg(head).arg(pl.pid).arg(rec_txt(pl.rec), v(pl.rid),
                                                                                    v(pl.continent), v(pl.climate),
                                                                                    v(pl.want.has_value()));
        };
        for (long long tpl : tpls)
            plan_line(u"plan %1 -> "_s.arg(tpl), plan(*d, defs, u"Neu %1"_s.arg(tpl), false, tpl));
        plan_line(u"plan sea -> "_s, plan(*d, defs, u"Mare"_s, true, std::nullopt));
        long long a_sea = 0;
        for (const auto &[p, pr] : m.provinces)
            if (pr.sea) {
                a_sea = p;
                break;
            }
        for (const std::optional<long long> &bad : {std::optional<long long>(), std::optional<long long>(a_sea)}) {
            try {
                plan(*d, defs, u"X"_s, false, bad);
                out << u"plan bad %1: no error"_s.arg(v(bad));
            } catch (const py::ValueError &e) {
                out << u"plan bad %1: %2"_s.arg(v(bad), QString::fromUtf8(e.what()));
            }
        }
        QFile fh(QDir(out_dir).filePath(mm.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mm.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static int run(const QString &root, const QString &backups, const QString &work)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QStringList REPLACE = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
    QStringList out;
    moddata::Mod m(root, BASE, std::nullopt, REPLACE);
    m.load();
    MapData md(m, m.p({u"map"_s, u"provinces.bmp"_s}));
    std::unique_ptr<document::Document> dp(doc_of(m));
    document::Document &d = *dp;
    auto defs_p = std::make_shared<ProvinceDefs>(m, md, *d.hist);
    ProvinceDefs &defs = *defs_p;
    d.maps.insert(u"definitions"_s, defs_p);
    auto pf_p = std::make_shared<positions::PositionsFile>(m, md.h);
    d.maps.insert(u"positions"_s, pf_p);
    auto bmp_p = std::make_shared<provbmp::ProvinceBitmap>(m, md);
    provbmp::ProvinceBitmap &bmp = *bmp_p;
    d.maps.insert(u"provinces_bmp"_s, bmp_p);
    auto &dm = d.map<mapfiles::DefaultMapFile>(u"default_map"_s);
    auto &cont_f = d.map<mapfiles::MembershipFile>(u"continent"_s);
    auto &clim_f = d.map<mapfiles::MembershipFile>(u"climate"_s);
    auto has_pops = [&](long long p) {
        const auto *l = d.pops.get(p);
        return l && !l->empty();
    };
    std::vector<long long> big;
    for (const auto &[p, pr] : m.provinces)
        if (!pr.sea && md.area[p] > 400 && has_pops(p))
            big.push_back(p);
    const long long A = big[7];
    std::vector<long long> nbA = md.neighbours(A);
    std::sort(nbA.begin(), nbA.end());
    long long B = 0;
    for (long long q : nbA)
        if (m.provinces.contains(q) && !m.provinces.get(q)->sea && has_pops(q)) {
            B = q;
            break;
        }
    const Plan pl = plan(d, defs, u"Neu Stadt; Ost"_s, false, A);
    const long long pid = pl.pid;
    out << u"plan %1 %2 rid %3 cont %4 clim %5"_s.arg(pid).arg(rec_txt(pl.rec), v(pl.rid), v(pl.continent), v(pl.climate));
    create_province(d, pid, pl.rec, pl.rid, pl.continent, pl.climate, pl.want);
    {
        const moddata::Province &pr = *m.provinces.get(pid);
        out << u"made %1 %2 %3 %4 %5 %6 %7"_s.arg(v(pr.name), v(pr.rgb), v(pr.sea), v(pr.owner), v(pr.cores),
                                                  v(pr.history_file), v(m.loc.contains(u"PROV%1"_s.arg(pid))
                                                                          ? OptStr(*m.loc.get(u"PROV%1"_s.arg(pid)))
                                                                          : std::nullopt));
        QStringList rels;
        if (const auto *fl = d.hist->files.get(pid))
            for (const auto &hf : *fl)
                rels << hf->rel;
        const std::vector<long long> seas(dm.sea.end() - std::min<size_t>(3, dm.sea.size()), dm.sea.end());
        QString reg = u"None"_s;
        if (pl.rid && m.regions.contains(*pl.rid)) {
            const auto &ps = m.regions.get(*pl.rid)->provinces;
            reg = v(std::vector<long long>(ps.end() - std::min<size_t>(3, ps.size()), ps.end()));
        }
        out << u"hist %1 sea %2 max %3 seas %4 region %5 cont %6 clim %7"_s.arg(
            v(rels), v(bool(md.sea[pid])), v(dm.max_provinces), v(seas), reg, v(cont_f.of(pid)), v(clim_f.of(pid)));
    }
    auto status = [&](long long p) {
        const auto s = new_status(defs, bmp, md, p);
        return pyform::L(s.first, s.second);
    };
    out << u"dirty %1 status %2"_s.arg(v(defs.dirty()), status(pid));
    const auto &flat = md.idmap.flat();
    std::vector<std::int64_t> pa, pb;
    for (size_t i = 0; i < flat.size(); ++i) {
        if (flat[i] == A)
            pa.push_back(std::int64_t(i));
        if (flat[i] == B)
            pb.push_back(std::int64_t(i));
    }
    std::vector<std::int64_t> idx(pa.begin(), pa.begin() + static_cast<long long>(double(pa.size()) * 0.6));
    idx.insert(idx.end(), pb.begin(), pb.begin() + 25);
    auto paint = [&](const QString &label, const std::vector<std::int64_t> &ix) {
        std::vector<std::uint16_t> old, nw;
        for (std::int64_t i : ix) {
            old.push_back(md.idmap.flat()[size_t(i)]);
            nw.push_back(std::uint16_t(pid));
        }
        d.edit(label, {}, [&] { bmp.commit(ix, old, nw); }, {}, {}, {u"provinces_bmp"_s});
    };
    paint(u"Paint"_s, idx);
    const auto donors = pixel_donors(bmp, pid);
    const auto shares = pop_shares(d, md, donors);
    out << u"painted area %1 status %2 donors %3 shares %4"_s.arg(md.area[pid]).arg(
        status(pid), v(std::vector<std::pair<long long, long long>>(donors.begin(), donors.end())), v(shares));
    positions::Entry e;
    e.pts.insert(u"unit"_s, {md.cx[pid] + 0.5, double(md.h) - md.cy[pid] - 0.5});
    e.pts.insert(u"text_position"_s, {md.cx[pid], double(md.h) - md.cy[pid]});
    std::vector<long long> sea_nb;
    for (long long q : md.neighbours(pid))
        if (m.provinces.contains(q) && m.provinces.get(q)->sea)
            sea_nb.push_back(q);
    std::sort(sea_nb.begin(), sea_nb.end());
    std::vector<adjacency::RowFields> specs;
    if (!sea_nb.empty())
        specs.push_back({sea_nb[0], pid, u"sea"_s, 0, 0, QString()});
    OrderedMap<long long, positions::Entry> entries;
    entries.insert(pid, e);
    const long long moved = finish_province(d, pid, shares, entries, specs, {}, bmp.version, md.area[pid]);
    {
        QStringList ps;
        if (const auto *l = d.pops.get(pid))
            for (const auto &p : *l)
                ps << pyform::L(p.type, p.culture, p.religion, p.size);
        out << u"finished moved %1 status %2 pops %3"_s.arg(moved).arg(status(pid), pyform::list(ps));
        auto sum = [&](long long q) {
            long long s = 0;
            if (const auto *l = d.pops.get(q))
                for (const auto &p : *l)
                    s += p.size;
            return s;
        };
        out << u"donor pops %1 %2"_s.arg(sum(A)).arg(sum(B));
    }
    const std::vector<std::int64_t> more(pb.begin() + 25, pb.begin() + 28);
    paint(u"Paint 2"_s, more);
    {
        const auto dn = pixel_donors(bmp, pid, defs.added.get(pid)->done_version);
        out << u"again status %1 donors %2"_s.arg(status(pid), v(std::vector<std::pair<long long, long long>>(dn.begin(), dn.end())));
    }
    const Plan sp = plan(d, defs, u"Mare Novum"_s, true, std::nullopt);
    const long long sid = sp.pid;
    create_province(d, sid, sp.rec);
    auto last3 = [](const std::vector<long long> &l) {
        return std::vector<long long>(l.end() - std::min<size_t>(3, l.size()), l.end());
    };
    out << u"sea %1 %2 dirty %3 seas %4"_s.arg(sid).arg(rec_txt(sp.rec), v(defs.dirty()), v(last3(dm.sea)));
    {
        const auto u = d.undo();
        out << u"undo %1 dirty %2 in %3"_s.arg(v(u), v(defs.dirty()), v(m.provinces.contains(sid)));
        const auto r = d.redo();
        out << u"redo %1 dirty %2 in %3"_s.arg(v(r), v(defs.dirty()), v(m.provinces.contains(sid)));
    }
    std::vector<long long> old_land;
    for (const auto &[p, pr] : m.provinces)
        if (!pr.sea && p < pid - 2)
            old_land.push_back(p);
    const long long C = old_land[40];
    long long Mx = 0;
    for (const auto &kv : m.provinces)
        if (kv.first < pid)
            Mx = std::max(Mx, kv.first);
    OrderedMap<long long, long long> heirs;
    heirs.insert(C, A);
    d.edit(u"Delete"_s, {}, [&] { defs.del({C, Mx}, heirs); }, {}, {}, {u"definitions"_s});
    out << u"deleted %1 %2 dirty %3 in %4 %5 next %6"_s.arg(C).arg(Mx).arg(v(defs.dirty()), v(m.provinces.contains(C)),
                                                                          v(m.provinces.contains(Mx)))
               .arg(defs.next_id());
    {
        const auto u = d.undo();
        out << u"undo %1 in %2"_s.arg(v(u), v(m.provinces.contains(C)));
        const auto r = d.redo();
        std::vector<long long> order;
        for (const auto &kv : m.provinces)
            order.push_back(kv.first);
        out << u"redo %1 in %2 order %3"_s.arg(v(r), v(m.provinces.contains(C)),
                                               v(std::vector<long long>(order.end() - 6, order.end())));
    }
    const auto [written, err] = d.save(backups);
    QStringList rel;
    for (const QString &p : written)
        rel << pypath::relpath(p, root).replace(u'\\', u'/');
    std::sort(rel.begin(), rel.end());
    OptStr bdir;
    if (err) {
        bdir = pypath::relpath(*err, backups).replace(u'\\', u'/');
        bdir->replace(QRegularExpression(u"\\d{8}_\\d{6}"_s), u"STAMP"_s);
    }
    out << u"saved %1 backups %2"_s.arg(v(rel), v(bdir));
    std::vector<std::pair<long long, moddata::Rgb>> fs_(m.free_slots.begin(), m.free_slots.end());
    std::sort(fs_.begin(), fs_.end());
    std::vector<std::pair<long long, std::optional<long long>>> hs(m.free_heirs.begin(), m.free_heirs.end());
    std::sort(hs.begin(), hs.end());
    out << u"after %1 free %2 heirs %3"_s.arg(v(defs.dirty()), v(fs_), v(hs));
    const QByteArray o = (out.join(u'\n') + u'\n').toUtf8();
    std::fwrite(o.constData(), 1, size_t(o.size()), stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 5 && a[1] == u"--run")
            return run(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
